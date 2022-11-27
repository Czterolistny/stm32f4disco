#include "flash.h"
#include "stm32f0xx.h"
#include "stm32f0xx_spi.h"
#include "stm32f0xx_gpio.h"
#include "stm32f0xx_rcc.h"
#include "spi.h"
#include <stdbool.h>

#define FLASH_WIP_BIT_IDX               (0x01u << 0u)

#define FLASH_ADDR_BYTES                (0x03u)
#define FLASH_GBUF_SIZE                 (0x04u)

#define flashSetAddrBuf(addr_24) ({ \
                                    do {\
                                        g_flashAddrBuf[0u] = (uint8_t)((addr_24 & 0xFF0000u) >> 16u); \
                                        g_flashAddrBuf[1u] = (uint8_t)((addr_24 & 0xFF00u) >> 8u); \
                                        g_flashAddrBuf[2u] = (uint8_t)(addr_24 & 0xFFu); \
                                    }while (0u); \
                                    (uint8_t *)&g_flashAddrBuf[0u]; \
                                })

#define flashGetGBufRef()     ((uint8_t *) &g_flashBuf[0u])

#define flashClearBuf()         ({ \
                                    do {\
                                        for(uint8_t i = 0u; i < FLASH_GBUF_SIZE; ++i ) \
                                            g_flashBuf[i] = 0x00u; \
                                    }while (0u); \
                                })

static bool g_flashBlockOperationInProgress = false;
volatile static uint8_t g_flashBuf[FLASH_GBUF_SIZE];
volatile static uint8_t g_flashAddrBuf[FLASH_ADDR_BYTES];

volatile static flashObj1 obj1 = {.param1 = 0xA5u, .param2 = 0xACDE};
flashAreaObj areaObj1 = {.addr = TEST_OBJ_ADDR, .size = TEST_OBJ_SIZE, .obj = (const void*)&obj1};

typedef struct{
    uint8_t cmd_id;
    uint8_t byteToWrite;
    int8_t byteToRead;
    volatile uint8_t *cmdBuf;
}FlashCmd;

typedef enum{
    FLASH_WR_ENABLE_CMD_IDX = 0,
    FLASH_WR_DISABLE_CMD_IDX,
    FLASH_READ_STATUS_REG_CMD_IDX,
    FLASH_WRITE_STATUS_REG_CMD_IDX,
    FLASH_READ_DATA_CMD_IDX,
    FLASH_WRITE_DATA_CMD_IDX,
    FLASH_MANUF_DEV_ID_CMD_IDX,
    FLASH_READ_IDENTIFIC_CMD_IDX,
    FLASH_READ_UNIQE_CMD_IDX,
    FLASH_SECTOR_ERASE_CMD_IDX
}CmdType;

static FlashCmd flashCmd[] = {
    /* Write Enable Cmd - FLASH_WR_ENABLE_CMD_IDX */
    {.cmd_id = 0x06, .byteToWrite = 0u, .byteToRead = 0, .cmdBuf = &g_flashBuf[0u]},
    /* Write Disable Cmd - FLASH_WR_DISABLE_CMD_IDX */
    {.cmd_id = 0x04, .byteToWrite = 0u, .byteToRead = 0, .cmdBuf = &g_flashBuf[0u]},
    /* Read Status Register Cmd - FLASH_READ_STATUS_REG_CMD_IDX */
    {.cmd_id = 0x05, .byteToWrite = 0u, .byteToRead = 1, .cmdBuf = &g_flashBuf[0u]},
    /* Write Status Register Cmd - FLASH_WRITE_STATUS_REG_CMD_IDX */
    {.cmd_id = 0x01, .byteToWrite = 1u, .byteToRead = 0, .cmdBuf = &g_flashBuf[0u]},
    /* Read Data Cmd - FLASH_READ_DATA_CMD_IDX */
    {.cmd_id = 0x03, .byteToWrite = 3u, .byteToRead = -1, .cmdBuf = &g_flashAddrBuf[0u]},
    /* Page Program Cmd - FLASH_WRITE_DATA_CMD_IDX */
    {.cmd_id = 0x02, .byteToWrite = 3u, .byteToRead = -1, .cmdBuf = &g_flashAddrBuf[0u]},
    /* Manufacturer/Device ID Cmd - FLASH_MANUF_DEV_ID_CMD_IDX */
    {.cmd_id = 0x90, .byteToWrite = 3u, .byteToRead = 2, .cmdBuf = &g_flashBuf[0u]},
    /* Read Identification Cmd - FLASH_READ_IDENTIFIC_CMD_IDX */
    {.cmd_id = 0x9F, .byteToWrite = 0u, .byteToRead = 4, .cmdBuf = &g_flashBuf[0u]},
    /* Read Unique ID Cmd - FLASH_READ_UNIQE_CMD_IDX */
    {.cmd_id = 0x4B, .byteToWrite = 4u, .byteToRead = 1, .cmdBuf = &g_flashBuf[0u]},
    /* Sector erase ID Cmd - FLASH_SECTOR_ERASE_CMD_IDX */
    {.cmd_id = 0x20, .byteToWrite = 3u, .byteToRead = 0, .cmdBuf = &g_flashAddrBuf[0u]}
    };

static void flashWriteCmd(CmdType cmd_idx)
{
    spiSetCS_Low();
    spiWriteByte(flashCmd[cmd_idx].cmd_id);
    spiWrite((uint8_t *) flashCmd[cmd_idx].cmdBuf, flashCmd[cmd_idx].byteToWrite);
    /* Do not finish transmission if command requires unexpected amount of byte to read */
    if( flashCmd[cmd_idx].byteToRead >= 0 )
    {
        spiRead((uint8_t *) flashCmd[cmd_idx].cmdBuf, flashCmd[cmd_idx].byteToRead);
        spiSetCS_High();
    }else{
        /* CS has to be set 'by hand' after trasmition get completed */
    }
}

inline static bool flashIsWriteInProgress(void)
{
    flashWriteCmd(FLASH_READ_STATUS_REG_CMD_IDX);
    return (bool) (FLASH_WIP_BIT_IDX & (flashGetGBufRef()[0u]));
}

static uint8_t flashGetInterChunkSize(uint32_t size, uint8_t chunk)
{
    static uint32_t bytesRead;
    uint8_t interChunk = chunk;
    if( size < chunk )
    {
        interChunk = size;
    }else
    {
        if( (bytesRead + interChunk ) > size)
        {
            interChunk = interChunk - (uint8_t)((bytesRead + (uint32_t)interChunk) - size);
        }
    }

    bytesRead += interChunk;
    if( bytesRead >= size )
    {
        bytesRead = 0u;
    }

    return interChunk;
}

void flashTest(void)
{
    flashWriteCmd(FLASH_READ_IDENTIFIC_CMD_IDX);
    flashWriteCmd(FLASH_READ_STATUS_REG_CMD_IDX);
}

void flashEraseSector(uint16_t sector)
{
    flashWriteCmd(FLASH_WR_ENABLE_CMD_IDX);
    (void) flashSetAddrBuf((uint32_t) sector);
    flashWriteCmd(FLASH_SECTOR_ERASE_CMD_IDX);
    while( true == flashIsWriteInProgress() ){};
}

uint8_t flashWriteBlock(uint32_t start_addr, uint32_t size, uint8_t *buf, uint8_t chunk)
{
    uint8_t interChunk;
    if( false == g_flashBlockOperationInProgress )
    {
        flashWriteCmd(FLASH_WR_ENABLE_CMD_IDX);

        while( true == flashIsWriteInProgress() ){};

        (void) flashSetAddrBuf(start_addr);
        flashWriteCmd(FLASH_WRITE_DATA_CMD_IDX);
        g_flashBlockOperationInProgress = true;
    }

    interChunk = flashGetInterChunkSize(size, chunk);
    spiWrite(buf, interChunk);
    if( interChunk <= chunk )
    {
        g_flashBlockOperationInProgress = false;
        spiSetCS_High();
        while( true == flashIsWriteInProgress() ){};
        flashWriteCmd(FLASH_WR_DISABLE_CMD_IDX);
        return 0;
    }
    return 1;
}

uint8_t flashReadBlock(uint32_t start_addr, uint32_t size, uint8_t *buf, uint8_t chunk)
{
    uint8_t interChunk;
    if( false == g_flashBlockOperationInProgress )
    {
        (void) flashSetAddrBuf(start_addr);
        flashWriteCmd(FLASH_READ_DATA_CMD_IDX);
        g_flashBlockOperationInProgress = true;
    }

    interChunk = flashGetInterChunkSize(size, chunk);
    spiRead(buf, interChunk);
    if( interChunk <= chunk )
    {
        g_flashBlockOperationInProgress = false;
        spiSetCS_High();
        return 0;
    }
    return 1;
}

void flashWriteAreaObj(flashAreaObj *areaObj)
{
    flashWriteBlock(areaObj->addr, areaObj->size, (uint8_t*) (areaObj->obj), areaObj->size);
}

void flashReadAreaObj(flashAreaObj *areaObj)
{
    flashReadBlock(areaObj->addr, areaObj->size, (uint8_t*) (areaObj->obj), areaObj->size);
}

void flashInit(void)
{
    spiInit();
    flashTest();

#if(1)
    flashEraseSector((uint16_t) 0x00u);
    flashWriteAreaObj(&areaObj1);
#endif

    flashReadAreaObj(&areaObj1);
    flashReadBlock(0x00u, FLASH_GBUF_SIZE, flashGetGBufRef(), FLASH_GBUF_SIZE);
}
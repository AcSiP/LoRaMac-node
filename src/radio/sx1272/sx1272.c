/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    �2013 Semtech

Description: Generic SX1272 driver implementation

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis and Gregory Cristian
*/
#include <math.h>
#include <string.h>
#include "board.h"
#include "radio.h"
#include "sx1272.h"
#include "sx1272-board.h"

/*
 * Local types definition
 */

/*!
 * Radio registers definition
 */
typedef struct
{
    RadioModems_t Modem;
    uint8_t       Addr;
    uint8_t       Value;
}RadioRegisters_t;

/*!
 * FSK bandwidth definition
 */
typedef struct
{
    uint32_t bandwidth;
    uint8_t  RegValue;
}FskBandwidth_t;


/*
 * Private functions prototypes
 */


/*!
 * \brief Resets the SX1272
 */
void SX1272Reset( void );

/*
 * \brief Sets the SX1272 in transmission mode for the given time
 * \param [IN] timeout      Transmission timeout [ms] [0: continuous, others timeout]
 */
void SX1272SetTx( uint32_t timeout );

/*!
 * \brief Writes the buffer contents to the SX1272 FIFO
 *
 * \param [IN] buffer Buffer containing data to be put on the FIFO.
 * \param [IN] size Number of bytes to be written to the FIFO
 */
void SX1272WriteFifo( uint8_t *buffer, uint8_t size );

/*!
 * \brief Reads the contents of the SX1272 FIFO
 *
 * \param [OUT] buffer Buffer where to copy the FIFO read data.
 * \param [IN] size Number of bytes to be read from the FIFO
 */
void SX1272ReadFifo( uint8_t *buffer, uint8_t size );

/*!
 * \brief Sets the SX1272 operating mode
 *
 * \param [IN] opMode New operating mode
 */
void SX1272SetOpMode( uint8_t opMode );

/*!
 * \brief Configures the SX1272 with the given modem
 *
 * \param [IN] modem Modem to be used [0: FSK, 1: LoRa] 
 */
void SX1272SetModem( RadioModems_t modem );

/*
 * SX1272 DIO IRQ callback functions prototype
 */

/*!
 * \brief DIO 0 IRQ callback
 */
void SX1272OnDio0Irq( void );

/*!
 * \brief DIO 1 IRQ callback
 */
void SX1272OnDio1Irq( void );

/*!
 * \brief DIO 2 IRQ callback
 */
void SX1272OnDio2Irq( void );

/*!
 * \brief DIO 3 IRQ callback
 */
void SX1272OnDio3Irq( void );

/*!
 * \brief DIO 4 IRQ callback
 */
void SX1272OnDio4Irq( void );

/*!
 * \brief DIO 5 IRQ callback
 */
void SX1272OnDio5Irq( void );

/*!
 * \brief Tx & Rx timeout timer callback
 */
void SX1272OnTimeoutIrq( void );

/*
 * Private global constants
 */

/*!
 * Radio hardware registers initialization
 * 
 * \remark RADIO_INIT_REGISTERS_VALUE is defined in sx1272-board.h file
 */
const RadioRegisters_t RadioRegsInit[] = RADIO_INIT_REGISTERS_VALUE;

/*!
 * Constant values need to compute the RSSI value
 */
#define NOISE_ABSOLUTE_ZERO                         -174.0
#define NOISE_FIGURE                                6.0

const double RssiOffset[] =
{
    -137.0,
    -133.0,
    -127.0,
};

/*!
 * Precomputed signal bandwidth 10*Log values
 * Used to compute the LoRa packet RSSI value.
 */
const double BandwidthLog[] =
{
    50.969100130080564143587833158265,
    53.97940008672037609572522210551,
    56.989700043360188047862611052755
};

/*!
 * Precomputed FSK bandwidth registers values
 */
const FskBandwidth_t FskBandwidths[] =
{       
    { 2600  , 0x17 },   
    { 3100  , 0x0F },
    { 3900  , 0x07 },
    { 5200  , 0x16 },
    { 6300  , 0x0E },
    { 7800  , 0x06 },
    { 10400 , 0x15 },
    { 12500 , 0x0D },
    { 15600 , 0x05 },
    { 20800 , 0x14 },
    { 25000 , 0x0C },
    { 31300 , 0x04 },
    { 41700 , 0x13 },
    { 50000 , 0x0B },
    { 62500 , 0x03 },
    { 83300 , 0x12 },
    { 100000, 0x0A },
    { 125000, 0x02 },
    { 166700, 0x11 },
    { 200000, 0x09 },
    { 250000, 0x01 }
};

/*
 * Private global variables
 */

/*!
 * Radio callbacks variable
 */
static RadioEvents_t *RadioEvents;

/*!
 * Reception buffer
 */
static uint8_t RxBuffer[RX_BUFFER_SIZE];

/*
 * Public global variables
 */

/*!
 * Radio hardware and global parameters
 */
SX1272_t SX1272;

/*!
 * Hardware DIO IRQ callback initialization
 */
DioIrqHandler *DioIrq[] = { SX1272OnDio0Irq, SX1272OnDio1Irq,
                            SX1272OnDio2Irq, SX1272OnDio3Irq,
                            SX1272OnDio4Irq, NULL };

/*!
 * Tx and Rx timers
 */
TimerEvent_t TxTimeoutTimer;
TimerEvent_t RxTimeoutTimer;

/*
 * Radio driver functions implementation
 */

void SX1272Init( RadioEvents_t *events )
{
    uint8_t i;

    RadioEvents = events;

    // Initialize driver timeout timers
    TimerInit( &TxTimeoutTimer, SX1272OnTimeoutIrq );
    TimerInit( &RxTimeoutTimer, SX1272OnTimeoutIrq );
    
    SX1272IoIrqInit( DioIrq );

    SX1272Reset( );

    SX1272SetOpMode( RF_OPMODE_SLEEP );

    for( i = 0; i < sizeof( RadioRegsInit ) / sizeof( RadioRegisters_t ); i++ )
    {
        SX1272SetModem( RadioRegsInit[i].Modem );
        SX1272Write( RadioRegsInit[i].Addr, RadioRegsInit[i].Value );
    }

    SX1272SetModem( MODEM_FSK );

    SX1272.Settings.State = RF_IDLE;
}

RadioState_t SX1272GetStatus( void )
{
    return SX1272.Settings.State;
}

void SX1272SetChannel( uint32_t freq )
{
    freq = ( uint32_t )( ( double )freq / ( double )FREQ_STEP );
    SX1272Write( REG_FRFMSB, ( uint8_t )( ( freq >> 16 ) & 0xFF ) );
    SX1272Write( REG_FRFMID, ( uint8_t )( ( freq >> 8 ) & 0xFF ) );
    SX1272Write( REG_FRFLSB, ( uint8_t )( freq & 0xFF ) );
}

bool SX1272IsChannelFree( RadioModems_t modem, uint32_t freq, int32_t rssiThresh )
{
    double rssi = 0;
    
    SX1272SetModem( modem );

    SX1272SetChannel( freq );
    
    SX1272SetOpMode( RF_OPMODE_RECEIVER );
    
    while( ( SX1272Read( REG_OPMODE ) & ~RF_OPMODE_MASK ) != RF_OPMODE_RECEIVER )
    {
    }
    
    rssi = SX1272ReadRssi( modem );
    
    SX1272SetSleep( );
    
    if( rssi > ( double )rssiThresh )
    {
        return false;
    }
    return true;
}

static uint8_t GetFskBandwidthRegValue( uint32_t bandwidth )
{
    uint8_t i;

    for( i = 0; i < ( sizeof( FskBandwidths ) / sizeof( FskBandwidth_t ) ) - 1; i++ )
    {
        if( ( bandwidth >= FskBandwidths[i].bandwidth ) && ( bandwidth < FskBandwidths[i + 1].bandwidth ) )
        {
            return FskBandwidths[i].RegValue;
        }
    }
    // ERROR: Value not found
    while( 1 );
}

void SX1272SetRxConfig( RadioModems_t modem, uint32_t bandwidth,
                         uint32_t datarate, uint8_t coderate,
                         uint32_t bandwidthAfc, bool fixLen,
                         bool crcOn, bool iqInverted, bool rxContinuous )
{
    SX1272SetModem( modem );

    switch( modem )
    {
    case MODEM_FSK:
        {
            SX1272.Settings.Fsk.Bandwidth = bandwidth;
            SX1272.Settings.Fsk.Datarate = datarate;
            SX1272.Settings.Fsk.BandwidthAfc = bandwidthAfc;
            SX1272.Settings.Fsk.FixLen = fixLen;
            SX1272.Settings.Fsk.CrcOn = crcOn;
            SX1272.Settings.Fsk.IqInverted = iqInverted;
            SX1272.Settings.Fsk.RxContinuous = rxContinuous;
            
            datarate = ( uint16_t )( ( double )XTAL_FREQ / ( double )datarate );
            SX1272Write( REG_BITRATEMSB, ( uint8_t )( datarate >> 8 ) );
            SX1272Write( REG_BITRATELSB, ( uint8_t )( datarate & 0xFF ) );

            SX1272Write( REG_RXBW, GetFskBandwidthRegValue( bandwidth ) );
            SX1272Write( REG_AFCBW, GetFskBandwidthRegValue( bandwidthAfc ) );

            SX1272Write( REG_PACKETCONFIG1,
                         ( SX1272Read( REG_PACKETCONFIG1 ) & 
                           RF_PACKETCONFIG1_CRC_MASK &
                           RF_PACKETCONFIG1_PACKETFORMAT_MASK ) |
                           ( ( fixLen == 1 ) ? RF_PACKETCONFIG1_PACKETFORMAT_FIXED : RF_PACKETCONFIG1_PACKETFORMAT_VARIABLE ) |
                           ( crcOn << 4 ) );
        }
        break;
    case MODEM_LORA:
        {
            SX1272.Settings.LoRa.Bandwidth = bandwidth;
            SX1272.Settings.LoRa.Datarate = datarate;
            SX1272.Settings.LoRa.Coderate = coderate;
            SX1272.Settings.LoRa.FixLen = fixLen;
            SX1272.Settings.LoRa.CrcOn = crcOn;
            SX1272.Settings.LoRa.IqInverted = iqInverted;
            SX1272.Settings.LoRa.RxContinuous = rxContinuous;

            if( datarate > 12 )
            {
                datarate = 12;
            }
            else if( datarate < 6 )
            {
                datarate = 6;
            }
        
            /* For low SF, we increase the number of symbol generating a Rx Timeout */
            if( datarate < 9 )
            {
                SX1272Write( REG_LR_SYMBTIMEOUTLSB, 0x08 );
            }
            else
            {
                SX1272Write( REG_LR_SYMBTIMEOUTLSB, 0x05 );
            }
        
            if( ( ( bandwidth == 0 ) && ( ( datarate == 11 ) || ( datarate == 12 ) ) ) ||
                ( ( bandwidth == 1 ) && ( datarate == 12 ) ) )
            {
                SX1272.Settings.LoRa.LowDatarateOptimize = 0x01;
            }
            else
            {
                SX1272.Settings.LoRa.LowDatarateOptimize = 0x00;
            }

            SX1272Write( REG_LR_MODEMCONFIG1, 
                         ( SX1272Read( REG_LR_MODEMCONFIG1 ) &
                           RFLR_MODEMCONFIG1_BW_MASK &
                           RFLR_MODEMCONFIG1_CODINGRATE_MASK &
                           RFLR_MODEMCONFIG1_IMPLICITHEADER_MASK &
                           RFLR_MODEMCONFIG1_RXPAYLOADCRC_MASK &
                           RFLR_MODEMCONFIG1_LOWDATARATEOPTIMIZE_MASK ) |
                           ( bandwidth << 6 ) | ( coderate << 3 ) | 
                           ( fixLen << 2 ) | ( crcOn << 1 ) |
                           SX1272.Settings.LoRa.LowDatarateOptimize );

            SX1272Write( REG_LR_MODEMCONFIG2,
                        ( SX1272Read( REG_LR_MODEMCONFIG2 ) &
                          RFLR_MODEMCONFIG2_SF_MASK ) |
                          ( datarate << 4 ) );

            if( datarate == 6 )
            {
                // Write NbTrigPeaks
                SX1272Write( 0x31, ( SX1272Read( 0x31 ) & 0xF8 ) | 5 );
            }
            else
            {
                // Write NbTrigPeaks
                SX1272Write( 0x31, ( SX1272Read( 0x31 ) & 0xF8 ) | 3 );
            }

            SX1272Write( 0x33, ( SX1272Read( 0x33 ) & 0xBF ) | ( iqInverted << 6 ) );
        }
        break;
    }
}

void SX1272SetTxConfig(  RadioModems_t modem, int8_t power, uint32_t fdev, 
                         uint32_t bandwidth, uint32_t datarate,
                         uint8_t coderate, uint16_t preambleLen,
                         bool fixLen, bool crcOn,
                         bool iqInverted, uint32_t timeout )
{
    uint8_t paConfig = 0;

    SX1272SetModem( modem );
    
    paConfig = SX1272Read( REG_PACONFIG );
    if( ( paConfig & RF_PACONFIG_PASELECT_PABOOST ) == RF_PACONFIG_PASELECT_PABOOST )
    {
        if( ( SX1272Read( REG_LR_PADAC ) & 0x07 ) == 0x07 )
        {
            if( power < 5 )
            {
                power = 5;
            }
            if( power > 20 )
            {
                power = 20;
            }
            paConfig = ( paConfig & RFLR_PACONFIG_OUTPUTPOWER_MASK ) | ( uint8_t )( ( uint16_t )( power - 5 ) & 0x0F );
        }
        else
        {
            if( power < 2 )
            {
                power = 2;
            }
            if( power > 17 )
            {
                power = 17;
            }
            paConfig = ( paConfig & RFLR_PACONFIG_OUTPUTPOWER_MASK ) | ( uint8_t )( ( uint16_t )( power - 2 ) & 0x0F );
        }
    }
    else
    {
        if( power < -1 )
        {
            power = -1;
        }
        if( power > 14 )
        {
            power = 14;
        }
        paConfig = ( paConfig & RFLR_PACONFIG_OUTPUTPOWER_MASK ) | ( uint8_t )( ( uint16_t )( power + 1 ) & 0x0F );
    }
    SX1272Write( REG_PACONFIG, paConfig );

    switch( modem )
    {
    case MODEM_FSK:
        {
            SX1272.Settings.Fsk.Power = power;
            SX1272.Settings.Fsk.Fdev = fdev;
            SX1272.Settings.Fsk.Bandwidth = bandwidth;
            SX1272.Settings.Fsk.Datarate = datarate;
            SX1272.Settings.Fsk.PreambleLen = preambleLen;
            SX1272.Settings.Fsk.FixLen = fixLen;
            SX1272.Settings.Fsk.CrcOn = crcOn;
            SX1272.Settings.Fsk.IqInverted = iqInverted;
            SX1272.Settings.Fsk.TxTimeout = timeout;
            
            fdev = ( uint16_t )( ( double )fdev / ( double )FREQ_STEP );
            SX1272Write( REG_FDEVMSB, ( uint8_t )( fdev >> 8 ) );
            SX1272Write( REG_FDEVLSB, ( uint8_t )( fdev & 0xFF ) );

            datarate = ( uint16_t )( ( double )XTAL_FREQ / ( double )datarate );
            SX1272Write( REG_BITRATEMSB, ( uint8_t )( datarate >> 8 ) );
            SX1272Write( REG_BITRATELSB, ( uint8_t )( datarate & 0xFF ) );

            SX1272Write( REG_PREAMBLEMSB, ( preambleLen >> 8 ) & 0x00FF );
            SX1272Write( REG_PREAMBLELSB, preambleLen & 0xFF );

            SX1272Write( REG_PACKETCONFIG1,
                         ( SX1272Read( REG_PACKETCONFIG1 ) & 
                           RF_PACKETCONFIG1_CRC_MASK &
                           RF_PACKETCONFIG1_PACKETFORMAT_MASK ) |
                           ( ( fixLen == 1 ) ? RF_PACKETCONFIG1_PACKETFORMAT_FIXED : RF_PACKETCONFIG1_PACKETFORMAT_VARIABLE ) |
                           ( crcOn << 4 ) );
        
        }
        break;
    case MODEM_LORA:
        {
            SX1272.Settings.LoRa.Power = power;
            SX1272.Settings.LoRa.Bandwidth = bandwidth;
            SX1272.Settings.LoRa.Datarate = datarate;
            SX1272.Settings.LoRa.Coderate = coderate;
            SX1272.Settings.LoRa.PreambleLen = preambleLen;
            SX1272.Settings.LoRa.FixLen = fixLen;
            SX1272.Settings.LoRa.CrcOn = crcOn;
            SX1272.Settings.LoRa.IqInverted = iqInverted;
            SX1272.Settings.LoRa.TxTimeout = timeout;

            if( datarate > 12 )
            {
                datarate = 12;
            }
            else if( datarate < 6 )
            {
                datarate = 6;
            }
            if( ( ( bandwidth == 0 ) && ( ( datarate == 11 ) || ( datarate == 12 ) ) ) ||
                ( ( bandwidth == 1 ) && ( datarate == 12 ) ) )
            {
                SX1272.Settings.LoRa.LowDatarateOptimize = 0x01;
            }
            else
            {
                SX1272.Settings.LoRa.LowDatarateOptimize = 0x00;
            }

            SX1272Write( REG_LR_MODEMCONFIG1, 
                         ( SX1272Read( REG_LR_MODEMCONFIG1 ) &
                           RFLR_MODEMCONFIG1_BW_MASK &
                           RFLR_MODEMCONFIG1_CODINGRATE_MASK &
                           RFLR_MODEMCONFIG1_IMPLICITHEADER_MASK &
                           RFLR_MODEMCONFIG1_RXPAYLOADCRC_MASK &
                           RFLR_MODEMCONFIG1_LOWDATARATEOPTIMIZE_MASK ) |
                           ( bandwidth << 6 ) | ( coderate << 3 ) | 
                           ( fixLen << 2 ) | ( crcOn << 1 ) |
                           SX1272.Settings.LoRa.LowDatarateOptimize );

            SX1272Write( REG_LR_MODEMCONFIG2,
                        ( SX1272Read( REG_LR_MODEMCONFIG2 ) &
                          RFLR_MODEMCONFIG2_SF_MASK ) |
                          ( datarate << 4 ) );

        
            SX1272Write( REG_LR_PREAMBLEMSB, ( preambleLen >> 8 ) & 0x00FF );
            SX1272Write( REG_LR_PREAMBLELSB, preambleLen & 0xFF );
            
            if( datarate == 6 )
            {
                // Write NbTrigPeaks
                SX1272Write( 0x31, ( SX1272Read( 0x31 ) & 0xF8 ) | 5 );
            }
            else
            {
                // Write NbTrigPeaks
                SX1272Write( 0x31, ( SX1272Read( 0x31 ) & 0xF8 ) | 3 );
            }

            SX1272Write( 0x33, ( SX1272Read( 0x33 ) & 0xBF ) | ( iqInverted << 6 ) );
        }
        break;
    }
}

double SX1272GetTimeOnAir( RadioModems_t modem, uint8_t pktLen )
{
    double airTime = 0.0;

    switch( modem )
    {
    case MODEM_FSK:
        {
            airTime = round( ( 8 * ( SX1272.Settings.Fsk.PreambleLen +
                                     ( ( SX1272Read( REG_SYNCCONFIG ) & ~RF_SYNCCONFIG_SYNCSIZE_MASK ) + 1 ) +
                                     ( ( SX1272.Settings.Fsk.FixLen == 0x01 ) ? 0.0 : 1.0 ) +
                                     ( ( ( SX1272Read( REG_PACKETCONFIG1 ) & ~RF_PACKETCONFIG1_ADDRSFILTERING_MASK ) != 0x00 ) ? 1.0 : 0 ) +
                                     pktLen +
                                     ( ( SX1272.Settings.Fsk.CrcOn == 0x01 ) ? 2.0 : 0 ) ) /
                                     SX1272.Settings.Fsk.Datarate ) * 1e3 );
        }
        break;
    case MODEM_LORA:
        {
            double bw = 0.0;
            switch( SX1272.Settings.LoRa.Bandwidth )
            {
            case 0: // 125 kHz
                bw = 125e3;
                break;
            case 1: // 250 kHz
                bw = 250e3;
                break;
            case 2: // 500 kHz
                bw = 500e3;
                break;
            }

            // Symbol rate : time for one symbol (secs)
            double rs = bw / ( 1 << SX1272.Settings.LoRa.Datarate );
            double ts = 1 / rs;
            // time of preamble
            double tPreamble = ( SX1272.Settings.LoRa.PreambleLen + 4.25 ) * ts;
            // Symbol length of payload and time
            double tmp = ceil( ( 8 * pktLen - 4 * SX1272.Settings.LoRa.Datarate +
                                 28 + 16 * SX1272.Settings.LoRa.CrcOn -
                                 ( SX1272.Settings.LoRa.FixLen ? 20 : 0 ) ) /
                                 ( double )( 4 * SX1272.Settings.LoRa.Datarate -
                                 ( ( SX1272.Settings.LoRa.LowDatarateOptimize > 0 ) ? 8 : 0 ) ) ) *
                                 ( SX1272.Settings.LoRa.Coderate + 4 );
            double nPayload = 8 + ( ( tmp > 0 ) ? tmp : 0 );
            double tPayload = nPayload * ts;
            // Time on air 
            double tOnAir = tPreamble + tPayload;
            // return us secs
            airTime = floor( tOnAir * 1e6 + 0.999 );
        }
        break;
    }
    return airTime;
}

void SX1272Send( uint8_t *buffer, uint8_t size )
{
    uint32_t txTimeout = 0;

    switch( SX1272.Settings.Modem )
    {
    case MODEM_FSK:
        {
            SX1272.Settings.FskPacketHandler.NbBytes = 0;
            SX1272.Settings.FskPacketHandler.Size = size;

            if( SX1272.Settings.Fsk.FixLen == false )
            {
                SX1272WriteFifo( ( uint8_t* )&size, 1 );
            }
            else
            {
                SX1272Write( REG_PAYLOADLENGTH, size );
            }            
            
            if( ( size > 0 ) && ( size <= 64 ) )
            {
                SX1272.Settings.FskPacketHandler.ChunkSize = size;
            }
            else
            {
                SX1272.Settings.FskPacketHandler.ChunkSize = 32;
            }

            // Write payload buffer
            SX1272WriteFifo( buffer, SX1272.Settings.FskPacketHandler.ChunkSize );
            SX1272.Settings.FskPacketHandler.NbBytes += SX1272.Settings.FskPacketHandler.ChunkSize;
            txTimeout = SX1272.Settings.Fsk.TxTimeout;
        }
        break;
    case MODEM_LORA:
        {
            SX1272.Settings.LoRaPacketHandler.Size = size;

            // Initializes the payload size
            SX1272Write( REG_LR_PAYLOADLENGTH, size );

            // Full buffer used for Tx            
            SX1272Write( REG_LR_FIFOTXBASEADDR, 0 );
            SX1272Write( REG_LR_FIFOADDRPTR, 0 );

            // Fifo operations can not take place in Sleep mode
            if( ( SX1272Read( REG_OPMODE ) & ~RF_OPMODE_MASK ) == RF_OPMODE_SLEEP )
            {
                SX1272SetStby( );
                //DelayMs( 1 );
                while( ( SX1272Read( REG_OPMODE ) & ~RF_OPMODE_MASK ) != RF_OPMODE_STANDBY );
            }
            // Write payload buffer
            SX1272WriteFifo( buffer, size );
            txTimeout = SX1272.Settings.LoRa.TxTimeout;
        }
        break;
    }

    SX1272SetTx( txTimeout );
}

void SX1272SetSleep( void )
{
    TimerStop( &RxTimeoutTimer );
    TimerStop( &TxTimeoutTimer );
    SX1272SetOpMode( RF_OPMODE_SLEEP );
}

void SX1272SetStby( void )
{
    TimerStop( &RxTimeoutTimer );
    TimerStop( &TxTimeoutTimer );
    SX1272SetOpMode( RF_OPMODE_STANDBY );
}

void SX1272SetRx( uint32_t timeout )
{
    bool rxContinuous = false;
    
    TimerSetValue( &RxTimeoutTimer, timeout );

    switch( SX1272.Settings.Modem )
    {
    case MODEM_FSK:
        {
            rxContinuous = SX1272.Settings.Fsk.RxContinuous;

            // DIO0=( CrcOn ) ? CrcOk : PayloadReady
            // DIO1=FifoLevel
            // DIO2=SyncAddr
            // DIO3=FifoEmpty
            // DIO4=Preamble
            // DIO5=ModeReady
            if( SX1272.Settings.Fsk.CrcOn == true )
            {
                SX1272Write( REG_DIOMAPPING1, ( SX1272Read( REG_DIOMAPPING1 ) & RF_DIOMAPPING1_DIO0_MASK &
                                                                                RF_DIOMAPPING1_DIO2_MASK ) | 
                                                                                RF_DIOMAPPING1_DIO0_01 |
                                                                                RF_DIOMAPPING1_DIO2_11 );
            }
            else
            {
                SX1272Write( REG_DIOMAPPING1, ( SX1272Read( REG_DIOMAPPING1 ) & RF_DIOMAPPING1_DIO0_MASK &
                                                                                RF_DIOMAPPING1_DIO2_MASK ) |
                                                                                RF_DIOMAPPING1_DIO0_00 |
                                                                                RF_DIOMAPPING1_DIO2_11 );
            }

            SX1272Write( REG_DIOMAPPING2, ( SX1272Read( REG_DIOMAPPING2 ) & RF_DIOMAPPING2_DIO4_MASK &
                                                                            RF_DIOMAPPING2_MAP_MASK ) | 
                                                                            RF_DIOMAPPING2_DIO4_11 |
                                                                            RF_DIOMAPPING2_MAP_PREAMBLEDETECT );

            SX1272.Settings.FskPacketHandler.PreambleDetected = false;
            SX1272.Settings.FskPacketHandler.SyncWordDetected = false;
            SX1272.Settings.FskPacketHandler.NbBytes = 0;
            SX1272.Settings.FskPacketHandler.Size = 0;
        }
        break;
    case MODEM_LORA:
        {
            rxContinuous = SX1272.Settings.LoRa.RxContinuous;

            SX1272Write( REG_LR_IRQFLAGSMASK, //RFLR_IRQFLAGS_RXTIMEOUT |
                                              //RFLR_IRQFLAGS_RXDONE |
                                              //RFLR_IRQFLAGS_PAYLOADCRCERROR |
                                              RFLR_IRQFLAGS_VALIDHEADER |
                                              RFLR_IRQFLAGS_TXDONE |
                                              RFLR_IRQFLAGS_CADDONE |
                                              RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL |
                                              RFLR_IRQFLAGS_CADDETECTED );

            // DIO0=RxDone
            SX1272Write( REG_DIOMAPPING1, ( SX1272Read( REG_DIOMAPPING1 ) & RFLR_DIOMAPPING1_DIO0_MASK ) | RFLR_DIOMAPPING1_DIO0_00 );
            SX1272Write( REG_LR_FIFORXBASEADDR, 0 );
            SX1272Write( REG_LR_FIFOADDRPTR, 0 );
        }
        break;
    }

    memset( RxBuffer, 0, ( size_t )RX_BUFFER_SIZE );

    SX1272.Settings.State = RF_RX_RUNNING;
    TimerStart( &RxTimeoutTimer );

#if defined( USE_DEBUG_PINS )
    GpioWrite( &DbgPin2, 1 );
#endif

    if( SX1272.Settings.Modem == MODEM_FSK )
    {
        SX1272SetOpMode( RF_OPMODE_RECEIVER );
    }
    else
    {
        if( rxContinuous == true )
        {
            SX1272SetOpMode( RFLR_OPMODE_RECEIVER );
        }
        else
        {
            SX1272SetOpMode( RFLR_OPMODE_RECEIVER_SINGLE );
        }
    }
}

void SX1272SetTx( uint32_t timeout )
{
    TimerSetValue( &TxTimeoutTimer, timeout );

    switch( SX1272.Settings.Modem )
    {
    case MODEM_FSK:
        {
            // DIO0=PacketSent
            // DIO1=FifoLevel
            // DIO2=FifoFull
            // DIO3=FifoEmpty
            // DIO4=LowBat
            // DIO5=ModeReady
            SX1272Write( REG_DIOMAPPING1, ( SX1272Read( REG_DIOMAPPING1 ) & RF_DIOMAPPING1_DIO0_MASK &
                                                                            RF_DIOMAPPING1_DIO2_MASK ) );

            SX1272Write( REG_DIOMAPPING2, ( SX1272Read( REG_DIOMAPPING2 ) & RF_DIOMAPPING2_DIO4_MASK &
                                                                            RF_DIOMAPPING2_MAP_MASK ) );
            SX1272.Settings.FskPacketHandler.FifoThresh = SX1272Read( REG_FIFOTHRESH ) & 0x3F;
        }
        break;
    case MODEM_LORA:
        {
            SX1272Write( REG_LR_IRQFLAGSMASK, RFLR_IRQFLAGS_RXTIMEOUT |
                                              RFLR_IRQFLAGS_RXDONE |
                                              RFLR_IRQFLAGS_PAYLOADCRCERROR |
                                              RFLR_IRQFLAGS_VALIDHEADER |
                                              //RFLR_IRQFLAGS_TXDONE |
                                              RFLR_IRQFLAGS_CADDONE |
                                              RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL |
                                              RFLR_IRQFLAGS_CADDETECTED );

            // DIO0=TxDone
            SX1272Write( REG_DIOMAPPING1, ( SX1272Read( REG_DIOMAPPING1 ) & RFLR_DIOMAPPING1_DIO0_MASK ) | RFLR_DIOMAPPING1_DIO0_01 );
        }
        break;
    }

    SX1272.Settings.State = RF_TX_RUNNING;
    //TimerStart( &TxTimeoutTimer );
    SX1272SetOpMode( RF_OPMODE_TRANSMITTER );
}

double SX1272ReadRssi( RadioModems_t modem )
{
    switch( modem )
    {
    case MODEM_FSK:
        return -( double )( ( double )SX1272Read( REG_RSSIVALUE ) / 2.0 );
    case MODEM_LORA:
        return RssiOffset[SX1272.Settings.LoRa.Bandwidth] + ( double )SX1272Read( REG_LR_RSSIVALUE );
    }
    return 255;
}

void SX1272Reset( void )
{
    // Set RESET pin to 1
    GpioInit( &SX1272.Reset, RADIO_RESET, PIN_OUTPUT, PIN_PUSH_PULL, PIN_NO_PULL, 1 );
    
    // Wait 1 ms
    DelayMs( 1 );

    // Configure RESET as input
    GpioInit( &SX1272.Reset, RADIO_RESET, PIN_INPUT, PIN_PUSH_PULL, PIN_NO_PULL, 1 );

    // Wait 6 ms
    DelayMs( 6 );
}

void SX1272SetOpMode( uint8_t opMode )
{
    static uint8_t opModePrev = RF_OPMODE_STANDBY;

    if( opMode != opModePrev )
    {
        opModePrev = opMode;
        if( opMode == RF_OPMODE_TRANSMITTER )
        {
             SX1272SetAntSw( 1 );
        }
        else
        {
            SX1272SetAntSw( 0 );
        }
        SX1272Write( REG_OPMODE, ( SX1272Read( REG_OPMODE ) & RF_OPMODE_MASK ) | opMode );
    }
}

void SX1272SetModem( RadioModems_t modem )
{
    if( SX1272.Settings.Modem == modem )
    {
        return;
    }

    SX1272.Settings.Modem = modem;
    switch( SX1272.Settings.Modem )
    {
    default:
    case MODEM_FSK:
        SX1272SetOpMode( RF_OPMODE_SLEEP );
        SX1272Write( REG_OPMODE, ( SX1272Read( REG_OPMODE ) & RFLR_OPMODE_LONGRANGEMODE_MASK ) | RFLR_OPMODE_LONGRANGEMODE_OFF );
    
        SX1272Write( REG_DIOMAPPING1, 0x00 );
        SX1272Write( REG_DIOMAPPING2, 0x30 ); // DIO5=ModeReady
        break;
    case MODEM_LORA:
        SX1272SetOpMode( RF_OPMODE_SLEEP );
        SX1272Write( REG_OPMODE, ( SX1272Read( REG_OPMODE ) & RFLR_OPMODE_LONGRANGEMODE_MASK ) | RFLR_OPMODE_LONGRANGEMODE_ON );

        SX1272Write( REG_DIOMAPPING1, 0x00 );
        SX1272Write( REG_DIOMAPPING2, 0x00 );
        break;
    }
}

void SX1272Write( uint8_t addr, uint8_t data )
{
    SX1272WriteBuffer( addr, &data, 1 );
}

uint8_t SX1272Read( uint8_t addr )
{
    uint8_t data;
    SX1272ReadBuffer( addr, &data, 1 );
    return data;
}

void SX1272WriteBuffer( uint8_t addr, uint8_t *buffer, uint8_t size )
{
    uint8_t i;

    //NSS = 0;
    GpioWrite( &SX1272.Spi.Nss, 0 );

    SpiInOut( &SX1272.Spi, addr | 0x80 );
    for( i = 0; i < size; i++ )
    {
        SpiInOut( &SX1272.Spi, buffer[i] );
    }

    //NSS = 1;
    GpioWrite( &SX1272.Spi.Nss, 1 );
}

void SX1272ReadBuffer( uint8_t addr, uint8_t *buffer, uint8_t size )
{
    uint8_t i;

    //NSS = 0;
    GpioWrite( &SX1272.Spi.Nss, 0 );

    SpiInOut( &SX1272.Spi, addr & 0x7F );

    for( i = 0; i < size; i++ )
    {
        buffer[i] = SpiInOut( &SX1272.Spi, 0 );
    }

    //NSS = 1;
    GpioWrite( &SX1272.Spi.Nss, 1 );
}

void SX1272WriteFifo( uint8_t *buffer, uint8_t size )
{
    SX1272WriteBuffer( 0, buffer, size );
}

void SX1272ReadFifo( uint8_t *buffer, uint8_t size )
{
    SX1272ReadBuffer( 0, buffer, size );
}

void SX1272OnTimeoutIrq( void )
{
    switch( SX1272.Settings.State )
    {
    case RF_RX_RUNNING:
        if( ( RadioEvents != NULL ) && ( RadioEvents->RxError != NULL ) )
        {
            RadioEvents->RxError( );
        }
        break;
    case RF_TX_RUNNING:
        if( ( RadioEvents != NULL ) && ( RadioEvents->TxTimeout != NULL ) )
        {
            RadioEvents->TxTimeout( );
        }
        break;
    default:
        break;
    }
    SX1272.Settings.State = RF_IDLE;
}

void SX1272OnDio0Irq( void )
{
#if defined( USE_DEBUG_PINS )
    GpioWrite( &DbgPin2, 0 );
#endif
    switch( SX1272.Settings.State )
    {                
        case RF_RX_RUNNING:
            //TimerStop( &RxTimeoutTimer );
            // RxDone interrupt
            switch( SX1272.Settings.Modem )
            {
            case MODEM_FSK:
                // Read received packet size
                if( ( SX1272.Settings.FskPacketHandler.Size == 0 ) && ( SX1272.Settings.FskPacketHandler.NbBytes == 0 ) )
                {
                    if( SX1272.Settings.Fsk.FixLen == false )
                    {
                        SX1272ReadFifo( ( uint8_t* )&SX1272.Settings.FskPacketHandler.Size, 1 );
                    }
                    else
                    {
                        SX1272.Settings.FskPacketHandler.Size = SX1272Read( REG_PAYLOADLENGTH );
                    }
                    SX1272ReadFifo( RxBuffer + SX1272.Settings.FskPacketHandler.NbBytes, SX1272.Settings.FskPacketHandler.Size - SX1272.Settings.FskPacketHandler.NbBytes );
                    SX1272.Settings.FskPacketHandler.NbBytes += ( SX1272.Settings.FskPacketHandler.Size - SX1272.Settings.FskPacketHandler.NbBytes );
                }
                else
                {
                    SX1272ReadFifo( RxBuffer + SX1272.Settings.FskPacketHandler.NbBytes, SX1272.Settings.FskPacketHandler.Size - SX1272.Settings.FskPacketHandler.NbBytes );
                    SX1272.Settings.FskPacketHandler.NbBytes += ( SX1272.Settings.FskPacketHandler.Size - SX1272.Settings.FskPacketHandler.NbBytes );
                }

                SX1272.Settings.State = RF_IDLE;

                if( ( RadioEvents != NULL ) && ( RadioEvents->RxDone != NULL ) )
                {
                    RadioEvents->RxDone( RxBuffer, &SX1272.Settings.FskPacketHandler.Size, SX1272.Settings.FskPacketHandler.RssiValue, 0, 0 ); 
                } 

                TimerStop( &RxTimeoutTimer );
                break;
            case MODEM_LORA:
                {
                    uint8_t irqFlags = 0;
                    uint8_t snr = 0;

                    // Clear Irq
                    SX1272Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_RXDONE  );

                    irqFlags = SX1272Read( REG_LR_IRQFLAGS );
                    if( ( irqFlags & RFLR_IRQFLAGS_PAYLOADCRCERROR_MASK ) == RFLR_IRQFLAGS_PAYLOADCRCERROR )
                    {
                        // Clear Irq
                        SX1272Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_PAYLOADCRCERROR  );

                        SX1272.Settings.State = RF_IDLE;

                        TimerStop( &RxTimeoutTimer );

                        if( ( RadioEvents != NULL ) && ( RadioEvents->RxError != NULL ) )
                        {
                            RadioEvents->RxError( ); 
                        }
                        break;
                    }

                    snr = SX1272Read( REG_LR_PKTSNRVALUE );
                    if( snr & 0x80 ) // The SNR sign bit is 1
                    {
                        // Invert and divide by 4
                        SX1272.Settings.LoRaPacketHandler.SnrValue = ( ( ~snr + 1 ) & 0xFF ) >> 2;
                        SX1272.Settings.LoRaPacketHandler.SnrValue = -SX1272.Settings.LoRaPacketHandler.SnrValue;
                    }
                    else
                    {
                        // Divide by 4
                        SX1272.Settings.LoRaPacketHandler.SnrValue = ( snr & 0xFF ) >> 2;
                    }


                    if( SX1272.Settings.LoRaPacketHandler.SnrValue < 0 )
                    {
                        SX1272.Settings.LoRaPacketHandler.RssiValue = NOISE_ABSOLUTE_ZERO +
                                                                      BandwidthLog[SX1272.Settings.LoRa.Bandwidth] +
                                                                      NOISE_FIGURE +
                                                                      ( double )SX1272.Settings.LoRaPacketHandler.SnrValue;
                    }
                    else
                    {    
                        SX1272.Settings.LoRaPacketHandler.RssiValue = RssiOffset[SX1272.Settings.LoRa.Bandwidth] +
                                                                      ( double )SX1272Read( REG_LR_PKTRSSIVALUE );
                    }

                    SX1272.Settings.LoRaPacketHandler.Size = SX1272Read( REG_LR_NBRXBYTES );
                    SX1272ReadFifo( RxBuffer, SX1272.Settings.LoRaPacketHandler.Size );
                
                    SX1272.Settings.State = RF_IDLE;

                    TimerStop( &RxTimeoutTimer );

                    if( ( RadioEvents != NULL ) && ( RadioEvents->RxDone != NULL ) )
                    {
                        RadioEvents->RxDone( RxBuffer, ( uint16_t* )&SX1272.Settings.LoRaPacketHandler.Size, SX1272.Settings.LoRaPacketHandler.RssiValue, SX1272.Settings.LoRaPacketHandler.SnrValue, snr );
                    }
                }
                break;
            default:
                break;
            }
            break;
        case RF_TX_RUNNING:
            TimerStop( &TxTimeoutTimer );
            // TxDone interrupt
            switch( SX1272.Settings.Modem )
            {
            case MODEM_LORA:
                // Clear Irq
                SX1272Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_TXDONE  );
                // Intentional fall through
            case MODEM_FSK:
            default:
                SX1272.Settings.State = RF_IDLE;
                if( ( RadioEvents != NULL ) && ( RadioEvents->TxDone != NULL ) )
                {
                    RadioEvents->TxDone( ); 
                } 
                break;
            }
            break;
        default:
            break;
    }
}

void SX1272OnDio1Irq( void )
{
    switch( SX1272.Settings.State )
    {                
        case RF_RX_RUNNING:
            switch( SX1272.Settings.Modem )
            {
            case MODEM_FSK:
                // FifoLevel interrupt
                // Read received packet size
                if( ( SX1272.Settings.FskPacketHandler.Size == 0 ) && ( SX1272.Settings.FskPacketHandler.NbBytes == 0 ) )
                {
                    
                    if( SX1272.Settings.Fsk.FixLen == false )
                    {
                        SX1272ReadFifo( ( uint8_t* )&SX1272.Settings.FskPacketHandler.Size, 1 );
                    }
                    else
                    {
                        SX1272.Settings.FskPacketHandler.Size = SX1272Read( REG_PAYLOADLENGTH );
                    }
                }

                if( ( SX1272.Settings.FskPacketHandler.Size - SX1272.Settings.FskPacketHandler.NbBytes ) > SX1272.Settings.FskPacketHandler.FifoThresh )
                {
                    SX1272ReadFifo( ( RxBuffer + SX1272.Settings.FskPacketHandler.NbBytes ), SX1272.Settings.FskPacketHandler.FifoThresh );
                    SX1272.Settings.FskPacketHandler.NbBytes += SX1272.Settings.FskPacketHandler.FifoThresh;
                }
                else
                {
                    SX1272ReadFifo( ( RxBuffer + SX1272.Settings.FskPacketHandler.NbBytes ), SX1272.Settings.FskPacketHandler.Size - SX1272.Settings.FskPacketHandler.NbBytes );
                    SX1272.Settings.FskPacketHandler.NbBytes += ( SX1272.Settings.FskPacketHandler.Size - SX1272.Settings.FskPacketHandler.NbBytes );
                }
                break;
            case MODEM_LORA:
#if defined( USE_DEBUG_PINS )
                GpioWrite( &DbgPin1, 1 );
                GpioWrite( &DbgPin2, 0 );
                GpioWrite( &DbgPin2, 1 );
                GpioWrite( &DbgPin2, 0 );
                GpioWrite( &DbgPin1, 0 );
#endif

                // Sync time out
                TimerStop( &RxTimeoutTimer );
                SX1272.Settings.State = RF_IDLE;
                if( ( RadioEvents != NULL ) && ( RadioEvents->RxTimeout != NULL ) )
                {
                    RadioEvents->RxTimeout( );
                }
                break;
            default:
                break;
            }
            break;
        case RF_TX_RUNNING:
            switch( SX1272.Settings.Modem )
            {
            case MODEM_FSK:
                // FifoLevel interrupt
                if( ( SX1272.Settings.FskPacketHandler.Size - SX1272.Settings.FskPacketHandler.NbBytes ) > SX1272.Settings.FskPacketHandler.ChunkSize )
                {
                    SX1272WriteFifo( ( RxBuffer + SX1272.Settings.FskPacketHandler.NbBytes ), SX1272.Settings.FskPacketHandler.ChunkSize );
                    SX1272.Settings.FskPacketHandler.NbBytes += SX1272.Settings.FskPacketHandler.ChunkSize;
                }
                else 
                {
                    // Write the last chunk of data
                    SX1272WriteFifo( RxBuffer + SX1272.Settings.FskPacketHandler.NbBytes, SX1272.Settings.FskPacketHandler.Size - SX1272.Settings.FskPacketHandler.NbBytes );
                    SX1272.Settings.FskPacketHandler.NbBytes += SX1272.Settings.FskPacketHandler.Size - SX1272.Settings.FskPacketHandler.NbBytes;
                }
                break;
            case MODEM_LORA:
                break;
            default:
                break;
            }
            break;      
        default:
            break;
    }
}

void SX1272OnDio2Irq( void )
{
    switch( SX1272.Settings.State )
    {                
        case RF_RX_RUNNING:
            switch( SX1272.Settings.Modem )
            {
            case MODEM_FSK:
                if( ( SX1272.Settings.FskPacketHandler.PreambleDetected == true ) && ( SX1272.Settings.FskPacketHandler.SyncWordDetected == false ) )
                {
                    SX1272.Settings.FskPacketHandler.SyncWordDetected = true;
                
                    SX1272.Settings.FskPacketHandler.RssiValue = -( double )( ( double )SX1272Read( REG_RSSIVALUE ) / 2.0 );

                    SX1272.Settings.FskPacketHandler.AfcValue = ( int32_t )( double )( ( ( uint16_t )SX1272Read( REG_AFCMSB ) << 8 ) |
                                                                           ( uint16_t )SX1272Read( REG_AFCLSB ) ) *
                                                                           ( double )FREQ_STEP;
                    SX1272.Settings.FskPacketHandler.RxGain = ( SX1272Read( REG_LNA ) >> 5 ) & 0x07;
                }
                break;
            case MODEM_LORA:
                break;
            default:
                break;
            }
            break;
        case RF_TX_RUNNING:
            switch( SX1272.Settings.Modem )
            {
            case MODEM_FSK:
                break;
            case MODEM_LORA:
                break;
            default:
                break;
            }
            break;      
        default:
            break;
    }
}

void SX1272OnDio3Irq( void )
{
    switch( SX1272.Settings.Modem )
    {
    case MODEM_FSK:
        break;
    case MODEM_LORA:
        break;
    default:
        break;
    }
}

void SX1272OnDio4Irq( void )
{
    switch( SX1272.Settings.Modem )
    {
    case MODEM_FSK:
        {
            if( SX1272.Settings.FskPacketHandler.PreambleDetected == false )
            {
                SX1272.Settings.FskPacketHandler.PreambleDetected = true;
            }    
        }
        break;
    case MODEM_LORA:
        break;
    default:
        break;
    }
}

void SX1272OnDio5Irq( void )
{
    switch( SX1272.Settings.Modem )
    {
    case MODEM_FSK:
        break;
    case MODEM_LORA:
        break;
    default:
        break;
    }
}

#include <Arduino.h>
#include <LoRaWan_APP.h>



#if defined(REGION_EU868)
#include "loramac/region/RegionEU868.h"
#elif defined(REGION_EU433)
#include "loramac/region/RegionEU433.h"
#elif defined(REGION_KR920)
#include "loramac/region/RegionKR920.h"
#elif defined(REGION_AS923) || defined(REGION_AS923_AS1) || defined(REGION_AS923_AS2)
#include "loramac/region/RegionAS923.h"
#endif


// loraWan default data rate when adr disabled
#ifdef REGION_US915
int8_t defaultDrForNoAdr = 3;
#else
int8_t defaultDrForNoAdr = 5;
#endif


uint8_t debugLevel = LoRaWAN_DEBUG_LEVEL;

// loraWan current data rate when adr disabled
int8_t currentDrForNoAdr;



// User application data buffer.
uint8_t appData[LORAWAN_APP_DATA_MAX_SIZE];

// User application data size to send.
uint8_t appDataSize = 4;

// Timer to handle rejoins.
// Must be called this because the precompiled library references it
// for some reason.
TimerEvent_t TxNextPacketTimer;



LoRaMacPrimitives_t LoRaMacPrimitive;
LoRaMacCallback_t LoRaMacCallback;

// Current state of the LoRaWan_APP state machine.
enum eDeviceState_LoraWan deviceState = DEVICE_STATE_OFF;


void (*JoinedDone)(void);
void (*SentDone)(uint8_t, bool);
void (*SendAcked)(void);
void (*ReceivedData)(McpsIndication_t *mcpsIndication);

// Returns true if the device is currently connected to a network,
// false otherwise.
static bool isJoinedToNetwork(void) {
  MibRequestConfirm_t mibReq;
  LoRaMacStatus_t status;

  mibReq.Type = MIB_NETWORK_JOINED;
  status      = LoRaMacMibGetRequestConfirm(&mibReq);

  if (status == LORAMAC_STATUS_OK) {
    if (mibReq.Param.IsNetworkJoined == true) {
      return true;
    }
  }
  return false;
}

// Callback when the rejoin timer fires.
static void TxNextPacketTimerEvent(void) {
  TimerStop(&TxNextPacketTimer);

  if (isJoinedToNetwork()) {
    deviceState = DEVICE_STATE_JOINED;
  } else {
    // Network not joined yet. Try to join again
    MlmeReq_t mlmeReq;
    mlmeReq.Type = MLME_JOIN;
    mlmeReq.Req.Join.DevEui   = devEui;
    mlmeReq.Req.Join.AppEui   = joinEui;
    mlmeReq.Req.Join.AppKey   = appKey;
    mlmeReq.Req.Join.NbTrials = 1;

    if ( LoRaMacMlmeRequest( &mlmeReq ) == LORAMAC_STATUS_OK ) {
      deviceState = DEVICE_STATE_JOINING;
    } else {
      deviceState = DEVICE_STATE_INIT_DONE;
    }
  }
}

/*!
 * \brief   MCPS-Confirm event function
 *
 * \param   [IN] mcpsConfirm - Pointer to the confirm structure,
 *               containing confirm attributes.
 */
static void McpsConfirm(McpsConfirm_t *mcpsConfirm) {
  if (mcpsConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
    switch (mcpsConfirm->McpsRequest) {
      case MCPS_UNCONFIRMED: {
        // Check Datarate
        // Check TxPower
        SentDone(1, false);
        break;
      }
      case MCPS_CONFIRMED: {
        // Check Datarate
        // Check TxPower
        // Check AckReceived
        // Check NbTrials
        SentDone(mcpsConfirm->NbRetries, mcpsConfirm->AckReceived);
        break;
      }
      case MCPS_PROPRIETARY: {
        break;
      }
      default:
        break;
    }
  }
  // Reset state and verify we got this callback when we expected.
  if (deviceState == DEVICE_STATE_SENDING) {
    deviceState = DEVICE_STATE_JOINED;
  }
}

// Callback after a transmission.
//
// \param   [IN] mcpsIndication - Pointer to the indication structure,
//               containing indication attributes.
static void McpsIndication( McpsIndication_t *mcpsIndication ) {
  if ( mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK ) {
    return;
  }

  printf( "received ");
  switch ( mcpsIndication->McpsIndication ) {
    case MCPS_UNCONFIRMED: {
      printf("unconfirmed ");
      break;
    }
    case MCPS_CONFIRMED: {
      printf("confirmed ");
      //   OnTxNextPacketTimerEvent( );
      break;
    }
    case MCPS_PROPRIETARY: {
      printf("proprietary ");
      break;
    }
    case MCPS_MULTICAST: {
      printf("multicast ");
      break;
    }
    default:
      break;
  }
  printf( "downlink: rssi = %d, snr = %d, datarate = %d\r\n", mcpsIndication->Rssi, (int)mcpsIndication->Snr,
          (int)mcpsIndication->RxDoneDatarate);

  if (mcpsIndication->AckReceived) {
    SendAcked();
  }

  if (mcpsIndication->RxData == true) {
    ReceivedData(mcpsIndication);
  }

  // Check Multicast
  // Check Port
  // Check Datarate
  // Check FramePending
  if (mcpsIndication->FramePending == true) {
    // The server signals that it has pending data to be sent.
    // We schedule an uplink as soon as possible to flush the server.
    // OnTxNextPacketTimerEvent();
  }
  // Check Buffer
  // Check BufferSize
  // Check Rssi
  // Check Snr
  // Check RxSlot

  delay(10);
}


// Callback after a join.
//
// \param   [IN] mlmeConfirm - Pointer to the confirm structure,
//               containing confirm attributes.
static void MlmeConfirm(MlmeConfirm_t *mlmeConfirm) {
  switch ( mlmeConfirm->MlmeRequest ) {
    case MLME_JOIN: {
      if ( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK ) {
        printf("joined\r\n");
        deviceState = DEVICE_STATE_JOINED;

        JoinedDone();
      } else {
        uint32_t rejoin_delay = 30000;
        printf("%d\n", mlmeConfirm->Status);
        printf("join failed, join again at 30s later\r\n");
        delay(5);
        TimerSetValue(&TxNextPacketTimer, rejoin_delay);
        TimerStart(&TxNextPacketTimer);
      }
      break;
    }
    case MLME_LINK_CHECK: {
      if ( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK ) {
        // Check DemodMargin
        // Check NbGateways
      }
      break;
    }
    case MLME_DEVICE_TIME: {
      if ( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK ) {
        dev_time_updated();
      }
      break;
    }
    default:
      break;
  }
}

/*!
 * \brief   MLME-Indication event function
 *
 * \param   [IN] mlmeIndication - Pointer to the indication structure.
 */
static void MlmeIndication( MlmeIndication_t *mlmeIndication ) {
  switch ( mlmeIndication->MlmeIndication ) {
    case MLME_SCHEDULE_UPLINK: {
      // The MAC signals that we shall provide an uplink as soon as possible
      //   OnTxNextPacketTimerEvent( );
      break;
    }
    default:
      break;
  }
}


void lwan_dev_params_update(void) {
#if defined(REGION_EU868)
  LoRaMacChannelAdd( 3, ( ChannelParams_t )EU868_LC4 );
  LoRaMacChannelAdd( 4, ( ChannelParams_t )EU868_LC5 );
  LoRaMacChannelAdd( 5, ( ChannelParams_t )EU868_LC6 );
  LoRaMacChannelAdd( 6, ( ChannelParams_t )EU868_LC7 );
  LoRaMacChannelAdd( 7, ( ChannelParams_t )EU868_LC8 );
#elif defined(REGION_EU433)
  LoRaMacChannelAdd( 3, ( ChannelParams_t )EU433_LC4 );
  LoRaMacChannelAdd( 4, ( ChannelParams_t )EU433_LC5 );
  LoRaMacChannelAdd( 5, ( ChannelParams_t )EU433_LC6 );
  LoRaMacChannelAdd( 6, ( ChannelParams_t )EU433_LC7 );
  LoRaMacChannelAdd( 7, ( ChannelParams_t )EU433_LC8 );
#elif defined(REGION_KR920)
  LoRaMacChannelAdd( 3, ( ChannelParams_t )KR920_LC4 );
  LoRaMacChannelAdd( 4, ( ChannelParams_t )KR920_LC5 );
  LoRaMacChannelAdd( 5, ( ChannelParams_t )KR920_LC6 );
  LoRaMacChannelAdd( 6, ( ChannelParams_t )KR920_LC7 );
  LoRaMacChannelAdd( 7, ( ChannelParams_t )KR920_LC8 );
#elif defined(REGION_AS923) || defined(REGION_AS923_AS1) || defined(REGION_AS923_AS2)
  LoRaMacChannelAdd( 2, ( ChannelParams_t )AS923_LC3 );
  LoRaMacChannelAdd( 3, ( ChannelParams_t )AS923_LC4 );
  LoRaMacChannelAdd( 4, ( ChannelParams_t )AS923_LC5 );
  LoRaMacChannelAdd( 5, ( ChannelParams_t )AS923_LC6 );
  LoRaMacChannelAdd( 6, ( ChannelParams_t )AS923_LC7 );
  LoRaMacChannelAdd( 7, ( ChannelParams_t )AS923_LC8 );
#endif

  MibRequestConfirm_t mibReq;

  mibReq.Type = MIB_CHANNELS_DEFAULT_MASK;
  mibReq.Param.ChannelsMask = userChannelsMask;
  LoRaMacMibSetRequestConfirm(&mibReq);

  mibReq.Type = MIB_CHANNELS_MASK;
  mibReq.Param.ChannelsMask = userChannelsMask;
  LoRaMacMibSetRequestConfirm(&mibReq);
}

void print_Hex(uint8_t *para, uint8_t size) {
  for (int i = 0; i < size; i++) {
    printf("%02X", *para++);
  }
}

void printLoRaWANStart(DeviceClass_t lorawanClass, LoRaMacRegion_t region) {
  Serial.println();
  Serial.print("LoRaWAN ");
  switch (region) {
    case LORAMAC_REGION_AS923_AS1:
      Serial.print("AS923(AS1:922.0-923.4MHz)");
      break;
    case LORAMAC_REGION_AS923_AS2:
      Serial.print("AS923(AS2:923.2-924.6MHz)");
      break;
    case LORAMAC_REGION_AU915:
      Serial.print("AU915");
      break;
    case LORAMAC_REGION_CN470:
      Serial.print("CN470");
      break;
    case LORAMAC_REGION_CN779:
      Serial.print("CN779");
      break;
    case LORAMAC_REGION_EU433:
      Serial.print("EU433");
      break;
    case LORAMAC_REGION_EU868:
      Serial.print("EU868");
      break;
    case LORAMAC_REGION_KR920:
      Serial.print("KR920");
      break;
    case LORAMAC_REGION_IN865:
      Serial.print("IN865");
      break;
    case LORAMAC_REGION_US915:
      Serial.print("US915");
      break;
    case LORAMAC_REGION_US915_HYBRID:
      Serial.print("US915_HYBRID ");
      break;
    default:
      break;
  }
  Serial.printf(" Class %X start!\r\n\r\n", lorawanClass + 10);
}

void printDevParam(void) {
  printf("+ChMask=%04X%04X%04X%04X%04X%04X\r\n", userChannelsMask[5], userChannelsMask[4], userChannelsMask[3],
         userChannelsMask[2], userChannelsMask[1], userChannelsMask[0]);
  printf("+DevEui=");
  print_Hex(devEui, 8);
  printf("(For OTAA Mode)\r\n");
  printf("+JoinEui=");
  print_Hex(joinEui, 8);
  printf("(For OTAA Mode)\r\n");
  printf("+AppKey=");
  print_Hex(appKey, 16);
  printf("(For OTAA Mode)\r\n");
  printf("+NwkSKey=");
  print_Hex(nwkSKey, 16);
  printf("(For ABP Mode)\r\n");
  printf("+AppSKey=");
  print_Hex(appSKey, 16);
  printf("(For ABP Mode)\r\n");
  printf("+DevAddr=%08X(For ABP Mode)\r\n\r\n", devAddr);
}



// void __attribute__((weak)) downLinkDataHandle(McpsIndication_t *mcpsIndication)
// {
//   printf("+REV DATA:%s,RXSIZE %d,PORT %d\r\n", mcpsIndication->RxSlot?"RXWIN2":"RXWIN1", mcpsIndication->BufferSize,
//          mcpsIndication->Port);
//   printf("+REV DATA:");
//   for (uint8_t i = 0; i < mcpsIndication->BufferSize; i++) {
//     printf("%02X", mcpsIndication->Buffer[i]);
//   }
//   printf("\r\n");
// }

void __attribute__((weak)) dev_time_updated()
{
  printf("device time updated\r\n");
}



void LoRaWanClass::generateDeveuiByChipID() {
  uint32_t uniqueId[2];
#if defined(ESP_PLATFORM)
  uint64_t id = getID();
  uniqueId[0] = (uint32_t)(id >> 32);
  uniqueId[1] = (uint32_t)id;
#endif
  for (int i = 0; i < 8; i++) {
    if (i < 4) {
      devEui[i] = (uniqueId[1] >> (8 * (3 - i))) & 0xFF;
    } else {
      devEui[i] = (uniqueId[0] >> (8 * (7 - i))) & 0xFF;
    }
  }
}


void LoRaWanClass::init(
	DeviceClass_t lorawanClass,
	LoRaMacRegion_t region,
	bool use_adr,
	void (*JoinedDoneCb)(void),
	void (*SentDoneCb)(uint8_t, bool),
    void (*SendAckedCb)(void),
    void (*ReceivedDataCb)(McpsIndication_t *mcpsIndication)) {

  if (region == LORAMAC_REGION_AS923_AS1 || region == LORAMAC_REGION_AS923_AS2) {
    region = LORAMAC_REGION_AS923;
  }

  // Save our state for this class.
  this->lorawan_region = region;
  this->lorawan_class  = lorawanClass;
  JoinedDone   = JoinedDoneCb;
  SentDone     = SentDoneCb;
  SendAcked    = SendAckedCb;
  ReceivedData = ReceivedDataCb;

  // Initialize our state for this class.
  this->join_callback_pending = false;

  MibRequestConfirm_t mibReq;

  // Initialize the library with callback function pointers.
  LoRaMacPrimitive.MacMcpsConfirm     = McpsConfirm;
  LoRaMacPrimitive.MacMcpsIndication  = McpsIndication;
  LoRaMacPrimitive.MacMlmeConfirm     = MlmeConfirm;
  LoRaMacPrimitive.MacMlmeIndication  = MlmeIndication;
  LoRaMacCallback.GetBatteryLevel     = BoardGetBatteryLevel;
  LoRaMacCallback.GetTemperatureLevel = NULL;
  LoRaMacInitialization(&LoRaMacPrimitive, &LoRaMacCallback, region);

  // Initialize a timer for retrying joins.
  TimerStop(&TxNextPacketTimer);
  TimerInit(&TxNextPacketTimer, TxNextPacketTimerEvent);

  if (!isJoinedToNetwork()) {
    // Print status info about this device's configuration.
    printLoRaWANStart(lorawanClass, region);
    printDevParam();
    Serial.println();

    // Set adaptive data rate settings.
    mibReq.Type = MIB_ADR;
    mibReq.Param.AdrEnable = use_adr;
    LoRaMacMibSetRequestConfirm(&mibReq);

    // Configure we are using a public lorawan network.
    mibReq.Type = MIB_PUBLIC_NETWORK;
    mibReq.Param.EnablePublicNetwork = LORAWAN_PUBLIC_NETWORK;
    LoRaMacMibSetRequestConfirm(&mibReq);

    // Set valid channels
    lwan_dev_params_update();

    // Check device class and ensure the desired class is selected.
    mibReq.Type = MIB_DEVICE_CLASS;
    LoRaMacMibGetRequestConfirm(&mibReq);
    if (mibReq.Param.Class != lorawanClass) {
      mibReq.Param.Class = lorawanClass;
      LoRaMacMibSetRequestConfirm(&mibReq);
    }

    // Our initialization is done.
    deviceState = DEVICE_STATE_INIT_DONE;
  } else {
    // The chip is already joined to a LoRaWAN network.
    deviceState = DEVICE_STATE_JOINED;
  }
}

void LoRaWanClass::join(bool useOverTheAirActivation) {
  // Check if we are already joined to a network.
  if (isJoinedToNetwork()) {
    deviceState = DEVICE_STATE_JOINED;

    // Need to mark that we need to issue the joined callback before going to sleep.
    this->join_callback_pending = true;

    return;
  }

  if (useOverTheAirActivation) {
    Serial.println("joining...");

    MlmeReq_t mlmeReq;
    mlmeReq.Type = MLME_JOIN;
    mlmeReq.Req.Join.DevEui   = devEui;
    mlmeReq.Req.Join.AppEui   = joinEui;
    mlmeReq.Req.Join.AppKey   = appKey;
    mlmeReq.Req.Join.NbTrials = 1;

    if (LoRaMacMlmeRequest(&mlmeReq) == LORAMAC_STATUS_OK) {
      deviceState = DEVICE_STATE_JOINING;
    } else {
      deviceState = DEVICE_STATE_INIT_DONE;
    }
  } else {
    MibRequestConfirm_t mibReq;

    mibReq.Type        = MIB_NET_ID;
    mibReq.Param.NetID = LORAWAN_NETWORK_ID;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_DEV_ADDR;
    mibReq.Param.DevAddr = devAddr;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_NWK_SKEY;
    mibReq.Param.NwkSKey = nwkSKey;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_APP_SKEY;
    mibReq.Param.AppSKey = appSKey;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_NETWORK_JOINED;
    mibReq.Param.IsNetworkJoined = true;
    LoRaMacMibSetRequestConfirm( &mibReq );

    deviceState = DEVICE_STATE_JOINED;
  }
}

void LoRaWanClass::send(bool confirmed, uint8_t trials, uint8_t app_port) {
  if (deviceState == DEVICE_STATE_JOINED) {
    McpsReq_t mcpsReq;
    LoRaMacTxInfo_t txInfo;

    if (LoRaMacQueryTxPossible(appDataSize, &txInfo) != LORAMAC_STATUS_OK) {
      // Send empty frame in order to flush MAC commands
      printf("payload length error ...\r\n");
      mcpsReq.Type = MCPS_UNCONFIRMED;
      mcpsReq.Req.Unconfirmed.fBuffer     = NULL;
      mcpsReq.Req.Unconfirmed.fBufferSize = 0;
      mcpsReq.Req.Unconfirmed.Datarate    = currentDrForNoAdr;
    } else {
      if (confirmed == false) {
        printf("unconfirmed uplink sending ...\r\n");
        mcpsReq.Type = MCPS_UNCONFIRMED;
        mcpsReq.Req.Unconfirmed.fPort       = app_port;
        mcpsReq.Req.Unconfirmed.fBuffer     = appData;
        mcpsReq.Req.Unconfirmed.fBufferSize = appDataSize;
        mcpsReq.Req.Unconfirmed.Datarate    = currentDrForNoAdr;
      } else {
        printf("confirmed uplink sending ...\r\n");
        mcpsReq.Type = MCPS_CONFIRMED;
        mcpsReq.Req.Confirmed.fPort       = app_port;
        mcpsReq.Req.Confirmed.fBuffer     = appData;
        mcpsReq.Req.Confirmed.fBufferSize = appDataSize;
        mcpsReq.Req.Confirmed.NbTrials    = trials;
        mcpsReq.Req.Confirmed.Datarate    = currentDrForNoAdr;
      }
    }

    if (LoRaMacMcpsRequest(&mcpsReq) == LORAMAC_STATUS_OK) {
      deviceState = DEVICE_STATE_SENDING;
    } else {
      deviceState = DEVICE_STATE_JOINED;
    }
  }
}

void LoRaWanClass::cycle(uint32_t timeout_ms) {
  // Use our internal timer to trigger a restart in `timeout_ms`.
  // Because `TxNextPacketTimer` is configured somehow as a low power timer,
  // the chip will go into "deep sleep" mode when this timer is pending. In deep sleep,
  // RAM is NOT preserved, and the chip essentially does a reboot when
  // it wakes up.
  TimerSetValue(&TxNextPacketTimer, timeout_ms);
  TimerStart(&TxNextPacketTimer);
}

void LoRaWanClass::sleep() {
  Radio.IrqProcess();

  if (this->join_callback_pending) {
    this->join_callback_pending = false;
    JoinedDone();
  } else {
    Mcu.sleep(this->lorawan_class, debugLevel);
  }
}

void LoRaWanClass::setDataRateForNoADR(int8_t dataRate) {
  defaultDrForNoAdr = dataRate;
}

LoRaWanClass LoRaWAN;

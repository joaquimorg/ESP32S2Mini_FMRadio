///
/// \file RDSParserLocal.cpp
/// \brief RDS Parser - copia local derivada da biblioteca Radio (Matthias Hertel).
///
/// Ver RDSParserLocal.h para a lista de alteracoes face ao original.
///

#include "RDSParserLocal.h"

/// Setup the RDS object and initialize private variables to 0.
RDSParserLocal::RDSParserLocal() {
  memset(this, 0, sizeof(RDSParserLocal));
}  // RDSParserLocal()


void RDSParserLocal::init() {
  strcpy(_PSName1, "11111111");
  strcpy(_PSName2, "22222222");
  strcpy(_PSName3, "33333333");
  strcpy(programServiceName, "        ");
  strcpy(lastServiceName, "        ");
  memset(_RDSText, 0, sizeof(_RDSText));
  _rdsTextRcv  = 0;
  _rdsTextDone = false;
}  // init()


void RDSParserLocal::attachServiceNameCallback(receiveServiceNameFunction newFunction) {
  _sendServiceName = newFunction;
}  // attachServiceNameCallback

void RDSParserLocal::attachTextCallback(receiveTextFunction newFunction) {
  _sendText = newFunction;
}  // attachTextCallback


void RDSParserLocal::attachTimeCallback(receiveTimeFunction newFunction) {
  _sendTime = newFunction;
}  // attachTimeCallback


void RDSParserLocal::processData(uint16_t block1, uint16_t block2, uint16_t block3, uint16_t block4) {
  uint8_t idx;  // index of rdsText

  uint16_t mins;  ///< RDS time in minutes
  uint8_t off;    ///< RDS time offset and sign

  if (block1 == 0) {
    // reset all the RDS info.
    init();
    // Send out empty data
    if (_sendServiceName) _sendServiceName(programServiceName);
    if (_sendText) _sendText("");
    return;
  }  // if

  // analyzing Block 2
  rdsGroupType = 0x0A | ((block2 & 0xF000) >> 8) | ((block2 & 0x0800) >> 11);
  rdsTP = (block2 & 0x0400);
  rdsPTY = (block2 & 0x0400);

  switch (rdsGroupType) {
    case 0x0A:
    case 0x0B: {
      // The data received is part of the Service Station Name
      idx = 2 * (block2 & 0x0003); // idx = 0, 2, 4, 6

      // new data is 2 chars from block 4
      char c1 = block4 >> 8;
      char c2 = block4 & 0x00FF;

      // shift new data into _PSNameN
      _PSName3[idx] = _PSName2[idx];
      _PSName2[idx] = _PSName1[idx];
      _PSName1[idx] = c1;

      _PSName3[idx+1] = _PSName2[idx+1];
      _PSName2[idx+1] = _PSName1[idx+1];
      _PSName1[idx+1] = c2;

      // check that the data was received successfully twice
      // before publishing the station name
      if (idx == 6) {
        bool isGood = true;
        // create programServiceName with 2 of 3
        for (int n= 0; n < 8; n++) {
          if ((_PSName1[n] == _PSName2[n]) || (_PSName1[n] == _PSName3[n])) {
            programServiceName[n] = _PSName1[n];

          } else if (_PSName2[n] == _PSName3[n]) {
            programServiceName[n] = _PSName2[n];

          } else {
            isGood = false;
          }
        }
        if ((isGood) && (strcmp(lastServiceName, programServiceName) != 0)) {
          strcpy(lastServiceName, programServiceName);
          if (_sendServiceName)
            _sendServiceName(programServiceName);
        }
      } // if
      break;
    }

    case 0x2A: {
      // The data received is part of the RDS Text.
      _textAB = (block2 & 0x0010);
      uint8_t seg = block2 & 0x000F;   // numero do segmento (0..15)
      idx = 4 * seg;                   // posicao no buffer (0,4,...,60)

      // Quando o bit A/B alterna, a mensagem mudou -> recomecar do zero.
      if (_textAB != _last_textAB) {
        _last_textAB = _textAB;
        memset(_RDSText, 0, sizeof(_RDSText));
        _rdsTextRcv  = 0;
        _rdsTextDone = false;
      }

      // Grava os 4 novos chars (2 do bloco 3, 2 do bloco 4) e marca o segmento.
      // Se o conteudo mudou, a mensagem deixa de estar "entregue" -> permite
      // reenviar a versao nova (cobre estacoes que nao alternam o bit A/B).
      {
        char nt[4] = { (char)(block3 >> 8), (char)(block3 & 0x00FF),
                       (char)(block4 >> 8), (char)(block4 & 0x00FF) };
        if (memcmp(&_RDSText[idx], nt, 4) != 0) {
          memcpy(&_RDSText[idx], nt, 4);
          _rdsTextDone = false;
        }
      }
      _rdsTextRcv |= (uint16_t)(1u << seg);

      // Numero de segmentos CONTIGUOS recebidos a partir do inicio.
      uint8_t nSeg = 0;
      while (nSeg < 16 && (_rdsTextRcv & (uint16_t)(1u << nSeg))) nSeg++;
      uint8_t avail = nSeg * 4;

      // Procura o terminador 0x0D dentro da zona contigua ja recebida.
      bool    hasTerm = false;
      uint8_t outLen  = avail;
      for (uint8_t i = 0; i < avail; i++) {
        if (_RDSText[i] == '\r') { outLen = i; hasTerm = true; break; }
      }

      // A mensagem esta COMPLETA quando: temos tudo ate ao terminador 0x0D, OU
      // recebemos os 16 segmentos (mensagem de 64 chars sem terminador). So
      // entao a entregamos - nunca mostramos prefixos parciais.
      bool complete = hasTerm || (_rdsTextRcv == 0xFFFF);
      if (complete && !_rdsTextDone) {
        if (!hasTerm) {                       // 64 chars: corta espacos finais
          outLen = 64;
          while (outLen > 0 && _RDSText[outLen - 1] == ' ') outLen--;
        }
        char out[65];
        memcpy(out, _RDSText, outLen);
        out[outLen] = '\0';
        _rdsTextDone = true;
        if (_sendText) _sendText(out);
      }
      break;
    }

    case 0x4A:
      // Clock time and date
      off = (block4)&0x3F;          // 6 bits
      mins = (block4 >> 6) & 0x3F;  // 6 bits
      mins += 60 * (((block3 & 0x0001) << 4) | ((block4 >> 12) & 0x0F));

      // adjust offset
      if (off & 0x20) {
        mins -= 30 * (off & 0x1F);
      } else {
        mins += 30 * (off & 0x1F);
      }

      if ((_sendTime) && (mins != _lastRDSMinutes)) {
        _lastRDSMinutes = mins;
        _sendTime(mins / 60, mins % 60);
      }  // if
      break;

    case 0x6A:  // IH
    case 0x8A:  // TMC
    case 0xAA:  // TMC
    case 0xCA:  // TMC
    case 0xEA:  // IH
      break;

    default:
      break;
  }
}  // processData()

// End.

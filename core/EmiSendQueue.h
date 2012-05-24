//
//  EmiSendQueue.h
//  roshambo
//
//  Created by Per Eckerdal on 2012-02-16.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#ifndef emilir_EmiSendQueue_h
#define emilir_EmiSendQueue_h

#include "EmiMessage.h"
#include "EmiNetUtil.h"
#include "EmiPacketHeader.h"
#include "EmiDataArrivalRate.h"
#include "EmiLinkCapacity.h"

#include <arpa/inet.h>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>

class EmiConnTime;

template<class SockDelegate, class ConnDelegate>
class EmiConn;

template<class SockDelegate, class ConnDelegate>
class EmiSendQueue {
    typedef typename SockDelegate::Binding   Binding;
    typedef typename Binding::PersistentData PersistentData;
    typedef EmiMessage<Binding>              EM;
    
    typedef std::vector<EM *> SendQueueVector;
    typedef typename std::vector<EM *>::iterator SendQueueVectorIter;
    typedef std::map<EmiChannelQualifier, EmiSequenceNumber> SendQueueAcksMap;
    typedef typename SendQueueAcksMap::iterator SendQueueAcksMapIter;
    typedef std::set<EmiChannelQualifier> SendQueueAcksSet;
    typedef EmiConn<SockDelegate, ConnDelegate> EC;
    
    EC& _conn;
    
    EmiPacketSequenceNumber _packetSequenceNumber;
    EmiPacketSequenceNumber _rttResponseSequenceNumber;
    EmiTimeInterval _rttResponseRegisterTime;
    SendQueueVector _queue;
    size_t _queueSize;
    SendQueueAcksMap _acks;
    // This set is intended to ensure that only one ack is sent per channel per tick
    SendQueueAcksSet _acksSentInThisTick;
    size_t _bufLength;
    uint8_t *_buf;
    bool _enqueueHeartbeat;
    EmiPacketSequenceNumber _enqueuedNak;
    
private:
    // Private copy constructor and assignment operator
    inline EmiSendQueue(const EmiSendQueue& other);
    inline EmiSendQueue& operator=(const EmiSendQueue& other);
    
    void clearQueue() {
        SendQueueVectorIter iter = _queue.begin();
        SendQueueVectorIter end = _queue.end();
        while (iter != end) {
            (*iter)->release();
            ++iter;
        }
        _queue.clear();
        _queueSize = 0;
    }
    
    void sendMessageInSeparatePacket(const EM *msg) {
        const uint8_t *data = Binding::extractData(msg->data);
        size_t dataLen = Binding::extractLength(msg->data);
        
        EmiMessage<Binding>::template writeControlPacketWithData<128>(msg->flags, data, dataLen, msg->sequenceNumber, ^(uint8_t *packetBuf, size_t size) {
            // Actually send the packet
            _conn.sendDatagram(packetBuf, size);
        });
    }
    
    void fillPacketHeaderData(EmiTimeInterval now,
                              EmiDataArrivalRate& dataArrivalRate,
                              EmiLinkCapacity& linkCapacity,
                              EmiConnTime& connTime,
                              EmiPacketHeader& packetHeader) {
        packetHeader.flags |= EMI_SEQUENCE_NUMBER_PACKET_FLAG;
        packetHeader.sequenceNumber = _packetSequenceNumber;
        _packetSequenceNumber = (_packetSequenceNumber+1) & EMI_PACKET_SEQUENCE_NUMBER_MASK;
        
        // Note that we only send RTT requests if a packet would be sent anyways.
        // This ensures that RTT data is sent only once per heartbeat if no data
        // is being transmitted.
        if (connTime.rttRequest(now, packetHeader.sequenceNumber)) {
            packetHeader.flags |= EMI_RTT_REQUEST_PACKET_FLAG;
            
            // Since we already have code that makes sure to send RTT requests
            // reasonably often, we piggyback on that to easily decide when to
            // calculate and send data arrival rate info: Whenever we send
            // RTT requests.
            packetHeader.flags |= EMI_ARRIVAL_RATE_PACKET_FLAG;
            packetHeader.arrivalRate = dataArrivalRate.calculate();
            
            // Ditto with link capacity; sending it whenever we send RTT
            // requests relieves us from having to cook up some separate logic
            // as to when to send that data.
            packetHeader.flags |= EMI_LINK_CAPACITY_PACKET_FLAG;
            packetHeader.linkCapacity = linkCapacity.calculate();
        }
        
        // Fill the packet header with a RTT response if we've been requested to do so
        if (-1 != _rttResponseSequenceNumber) {
            packetHeader.flags |= EMI_RTT_RESPONSE_PACKET_FLAG;
            packetHeader.rttResponse = _rttResponseSequenceNumber;
            
            EmiTimeInterval delay = (now-_rttResponseRegisterTime)*1000;
            if (delay < 0) delay = 0;
            if (delay > EMI_PACKET_HEADER_MAX_RESPONSE_DELAY) delay = EMI_PACKET_HEADER_MAX_RESPONSE_DELAY;
            
            packetHeader.rttResponseDelay = (uint8_t) std::floor(delay);
            
            _rttResponseSequenceNumber = -1;
            _rttResponseRegisterTime = 0;
        }
        
        if (-1 != _enqueuedNak) {
            packetHeader.flags |= EMI_NAK_PACKET_FLAG;
            packetHeader.nak = _enqueuedNak;
        }
    }
    
    // Returns true if something was sent
    bool flush(EmiDataArrivalRate& dataArrivalRate,
               EmiLinkCapacity& linkCapacity,
               EmiConnTime& connTime,
               EmiTimeInterval now) {
        bool sentPacket = false;
        
        if (!_queue.empty() || !_acks.empty()) {
            EmiPacketHeader packetHeader;
            fillPacketHeaderData(now, dataArrivalRate, linkCapacity, connTime, packetHeader);
            size_t packetHeaderLength;
            EmiPacketHeader::write(_buf, _bufLength, packetHeader, &packetHeaderLength);
            
            size_t pos = packetHeaderLength;
            
            /// Send the enqueued messages
            SendQueueAcksMapIter noAck = _acks.end();
            SendQueueVectorIter  iter  = _queue.begin();
            SendQueueVectorIter  end   = _queue.end();
            while (iter != end) {
                EM *msg = *iter;
                
                SendQueueAcksMapIter curAck;
                if (0 != _acksSentInThisTick.count(msg->channelQualifier)) {
                    // Only send an ack for a particular channel once per packet
                    curAck = noAck;
                }
                else {
                    curAck = _acks.find(msg->channelQualifier);
                }
                
                _acksSentInThisTick.insert(msg->channelQualifier);
                _acks.erase(msg->channelQualifier);
                
                bool hasAck = curAck != noAck;
                pos += EM::writeMsg(_buf, /* buf */
                                    _bufLength, /* bufSize */
                                    pos, /* offset */
                                    hasAck, /* hasAck */
                                    hasAck && (*curAck).second, /* ack */
                                    msg->channelQualifier,
                                    msg->sequenceNumber,
                                    Binding::extractData(msg->data),
                                    Binding::extractLength(msg->data),
                                    msg->flags);
                
                ++iter;
            }
            
            /// Send ACK messages without data for the acks that are
            /// enqueued but was not sent along with actual data.
            SendQueueAcksMapIter ackIter = _acks.begin();
            SendQueueAcksMapIter ackEnd = _acks.end();
            while (ackIter != ackEnd) {
                EmiChannelQualifier cq = (*ackIter).first;
                
                if (0 == _acksSentInThisTick.count(cq)) {
                    EmiSequenceNumber sn = (*ackIter).second;
                    
                    pos += EM::writeMsg(_buf, /* buf */
                                        _bufLength, /* bufSize */
                                        pos, /* offset */
                                        true, /* hasAck */
                                        sn, /* ack */
                                        cq, /* channelQualifier */
                                        0, /* sequenceNumber */
                                        NULL, /* data */
                                        0, /* dataLength */
                                        0 /* flags */);
                    
                    _acksSentInThisTick.insert(cq);
                    _acks.erase(cq);
                }
                
                ++ackIter;
            }
            
            if (packetHeaderLength != pos) {
                ASSERT(pos <= _bufLength);
                
                _conn.sendDatagram(_buf, pos);
                sentPacket = true;
            }
        }
        
        if (!sentPacket && _enqueueHeartbeat) {
            // Send heartbeat
            sendHeartbeat(dataArrivalRate, linkCapacity, connTime, now);
            sentPacket = true;
        }
        
        clearQueue();
        _enqueueHeartbeat = false;
        
        return sentPacket;
    }
    
public:
    
    EmiSendQueue(EC& conn) :
    _conn(conn),
    _packetSequenceNumber(arc4random() & EMI_PACKET_SEQUENCE_NUMBER_MASK),
    _rttResponseSequenceNumber(-1),
    _rttResponseRegisterTime(0),
    _enqueueHeartbeat(false),
    _enqueuedNak(-1),
    _queueSize(0) {
        _bufLength = conn.getEmiSock().config.mtu;
        _buf = (uint8_t *)malloc(_bufLength);
    }
    virtual ~EmiSendQueue() {
        clearQueue();
        
        _enqueueHeartbeat = false;
        
        if (NULL != _buf) {
            _bufLength = 0;
            free(_buf);
            _buf = NULL;
        }
    }
    
    void enqueueHeartbeat() {
        _enqueueHeartbeat = true;
    }
    
    void enqueueNak(EmiPacketSequenceNumber nak) {
        _enqueuedNak = nak;
    }
    
    void sendHeartbeat(EmiDataArrivalRate& dataArrivalRate,
                       EmiLinkCapacity& linkCapacity,
                       EmiConnTime& connTime,
                       EmiTimeInterval now) {
        if (_conn.isOpen()) {
            EmiPacketHeader ph;
            fillPacketHeaderData(now, dataArrivalRate, linkCapacity, connTime, ph);
            
            uint8_t buf[32];
            size_t packetLength;
            EmiPacketHeader::write(buf, sizeof(buf), ph, &packetLength);
            
            _conn.sendDatagram(buf, packetLength);
        }
    }
    
    // Returns true if something was sent
    bool tick(EmiDataArrivalRate& dataArrivalRate,
              EmiLinkCapacity& linkCapacity,
              EmiConnTime& connTime,
              EmiTimeInterval now) {
        _acksSentInThisTick.clear();
        
        bool somethingWasSent = flush(dataArrivalRate, linkCapacity, connTime, now);
        
        return somethingWasSent;
    }
    
    // Returns true if at least 1 ack is now enqueued
    bool enqueueAck(EmiChannelQualifier channelQualifier, EmiSequenceNumber sequenceNumber) {
        SendQueueAcksMapIter ackCur = _acks.find(channelQualifier);
        SendQueueAcksMapIter ackEnd = _acks.end();
        
        if (ackCur == ackEnd) {
            _acks[channelQualifier] = sequenceNumber;
        }
        else {
            _acks[channelQualifier] = EmiNetUtil::cyclicMax16((*ackCur).second, sequenceNumber);
        }
        
        return !_acks.empty();
    }
    
    void enqueueRttResponse(EmiPacketSequenceNumber sequenceNumber, EmiTimeInterval now) {
        _rttResponseSequenceNumber = sequenceNumber;
        _rttResponseRegisterTime = now;
    }
    
    void enqueueMessage(EM *msg,
                        EmiDataArrivalRate& dataArrivalRate,
                        EmiLinkCapacity& linkCapacity,
                        EmiConnTime& connTime,
                        EmiTimeInterval now) {
        if (msg->flags & (EMI_PRX_FLAG | EMI_RST_FLAG | EMI_SYN_FLAG)) {
            // This is a control message, one that cannot be bundled with
            // other messages. We might just as well send it right away.
            sendMessageInSeparatePacket(msg);
        }
        else {
            // Only EMI_PRIORITY_HIGH messages are implemented
            ASSERT(EMI_PRIORITY_HIGH == msg->priority);
            
            size_t msgSize = msg->approximateSize();
            
            if (_queueSize + msgSize >= _bufLength) { // _bufLength is the MTU of the EmiSocket
                flush(dataArrivalRate, linkCapacity, connTime, now);
            }
            
            msg->retain();
            _queue.push_back(msg);
            _queueSize += msgSize;
        }
    }
};

#endif

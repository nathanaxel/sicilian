// Copyright 2021 Optiver Asia Pacific Pty. Ltd.
//
// This file is part of Ready Trader Go.
//
//     Ready Trader Go is free software: you can redistribute it and/or
//     modify it under the terms of the GNU Affero General Public License
//     as published by the Free Software Foundation, either version 3 of
//     the License, or (at your option) any later version.
//
//     Ready Trader Go is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU Affero General Public License for more details.
//
//     You should have received a copy of the GNU Affero General Public
//     License along with Ready Trader Go.  If not, see
//     <https://www.gnu.org/licenses/>.
#include <ready_trader_go/logging.h>

#include <array>
#include <boost/asio/io_context.hpp>
#include <iostream>

#include "autotraderFinal.h"

using namespace ReadyTraderGo;

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

constexpr int POSITION_LIMIT = 100;
constexpr int TICK_SIZE_IN_CENTS = 100;
constexpr int MIN_BID_NEAREST_TICK = (MINIMUM_BID + TICK_SIZE_IN_CENTS) / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;
constexpr int MAX_ASK_NEAREST_TICK = MAXIMUM_ASK / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;

AutoTrader::AutoTrader(boost::asio::io_context& context): BaseAutoTrader(context) {}

void AutoTrader::DisconnectHandler() {
    BaseAutoTrader::DisconnectHandler();
    RLOG(LG_AT, LogLevel::LL_INFO) << "execution connection lost";
}

void AutoTrader::ErrorMessageHandler(unsigned long clientOrderId, const std::string& errorMessage) {
    RLOG(LG_AT, LogLevel::LL_INFO) << "error with order " << clientOrderId << ": " << errorMessage;
    if (clientOrderId != 0 && ((mAsks.count(clientOrderId) == 1) ||
                               (mBids.count(clientOrderId) == 1))) {
        OrderStatusMessageHandler(clientOrderId, 0, 0, 0);
    }
}

void AutoTrader::HedgeFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume) {
}

void AutoTrader::OrderBookMessageHandler(
    Instrument instrument, unsigned long sequenceNumber,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes) {
    // Use FUTURE (liquid order book) prices to set prices for ETF (illiquid
    // order book)
    if (instrument == Instrument::FUTURE) {


        // set bid / ask price + transaction fee
        unsigned long newAskPrice = askPrices[0] + TICK_SIZE_IN_CENTS;
        unsigned long newBidPrice = bidPrices[0] - TICK_SIZE_IN_CENTS;


        // cancel existing order if price set is different from previous setted price
        if (mAskId != 0 && newAskPrice != 0 && newAskPrice != mAskPrice) {  
            SendCancelOrder(mAskId);
            mAskId = 0;
        }
        if (mBidId != 0 && newBidPrice != mBidPrice) {  
            SendCancelOrder(mBidId);
            mBidId = 0;
        }

        unsigned long askVolume = (POSITION_LIMIT + mPosition) / 2;
        unsigned long bidVolume = TICK_SIZE_IN_CENTS - askVolume;
            
        // create new order with the new setted price
        // should only have 1 order for buy / sell each
        if (mAskId == 0 && askVolume) {
            SendInsertOrder(++mNextMessageId, Side::SELL, newAskPrice, askVolume, Lifespan::GOOD_FOR_DAY);
            mAskPrice = newAskPrice;
            mAskId = mNextMessageId;
            mAsks.emplace(mAskId);
        }
        if (mBidId == 0 && bidVolume) {
            SendInsertOrder(++mNextMessageId, Side::BUY, newBidPrice, bidVolume, Lifespan::GOOD_FOR_DAY);
            mBidPrice = newBidPrice;
            mBidId = mNextMessageId;
            mBids.emplace(mBidId);
        }
    }
}


void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume) {

    // hedge order when order is filled
    if (mAsks.count(clientOrderId)) {
        mPosition -= (long)volume;
        SendHedgeOrder(++mNextMessageId, Side::BUY, MAX_ASK_NEAREST_TICK,volume);
    } else if (mBids.count(clientOrderId)) {
        mPosition += (long)volume;
        SendHedgeOrder(++mNextMessageId, Side::SELL, MIN_BID_NEAREST_TICK,volume);
    }
}

void AutoTrader::OrderStatusMessageHandler(unsigned long clientOrderId,
                                           unsigned long fillVolume,
                                           unsigned long remainingVolume,
                                           signed long fees) {
    if (remainingVolume == 0) {
        if (clientOrderId == mAskId) {
            mAskId = 0;
        } else if (clientOrderId == mBidId) {
            mBidId = 0;
        }

        mAsks.erase(clientOrderId);
        mBids.erase(clientOrderId);
    }
}

void AutoTrader::TradeTicksMessageHandler(
    Instrument instrument, unsigned long sequenceNumber,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes) {
}

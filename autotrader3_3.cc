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

#include "autotrader3_3.h"

using namespace ReadyTraderGo;

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

constexpr int POSITION_LIMIT = 100;
constexpr int TICK_SIZE_IN_CENTS = 100;
constexpr int MIN_BID_NEAREST_TICK = (MINIMUM_BID + TICK_SIZE_IN_CENTS) / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;
constexpr int MAX_ASK_NEAREST_TICK = MAXIMUM_ASK / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;

constexpr double TAKER_FEE = 0.0002;
constexpr double MAKER_FEE = -0.0001;

signed long runroundCeilHundredth(signed long d);
signed long runroundFloorHundredth(signed long d);
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

        // prevent buying / selling if we have significant short/long position
        // to prevent overlimit mPosition: position in ETF by rules: you can
        // only have -100 <= mPosition <= 100
        allowBuy = (mPosition < POSITION_LIMIT);
        allowSell = (mPosition > -POSITION_LIMIT);

        // set bid / ask price + transaction fee
        double transactionFee = (TAKER_FEE + MAKER_FEE);
        unsigned long newAskPrice = (askPrices[0] != 0) ? (askPrices[0] * (1 + transactionFee)) : 0;
        unsigned long newBidPrice = (bidPrices[0] != 0) ? (bidPrices[0] * (1 - transactionFee)) : 0;

        // set bid / ask price + profits
        newAskPrice = runroundCeilHundredth(newAskPrice);
        newBidPrice = runroundFloorHundredth(newBidPrice);

        // cancel existing order if price set is different from previous setted price
        if (mAskId != 0 && newAskPrice != 0 && newAskPrice != mAskPrice) {  
            SendCancelOrder(mAskId);
        }
        if (mBidId != 0 && newBidPrice != 0 && newBidPrice != mBidPrice) {  
            SendCancelOrder(mBidId);
        }

        //confirm that order has been cancelled
        if (!mAsks.count(mAskId))  
            mAskId = 0;
        if (!mBids.count(mBidId))
            mBidId = 0;

        // create new order with the new setted price
        // should only have 1 order for buy / sell each
        if (allowSell && mAskId == 0 && newAskPrice != 0) {
            mAskId = mNextMessageId++;
            mAskPrice = newAskPrice;
            SendInsertOrder(mAskId, Side::SELL, newAskPrice, POSITION_LIMIT + mPosition, Lifespan::GOOD_FOR_DAY);
            mAsks.emplace(mAskId);
        }
        if (allowBuy && mBidId == 0 && newBidPrice != 0) {
            mBidId = mNextMessageId++;
            mBidPrice = newBidPrice;
            SendInsertOrder(mBidId, Side::BUY, newBidPrice, POSITION_LIMIT - mPosition, Lifespan::GOOD_FOR_DAY);
            mBids.emplace(mBidId);
        }
    }
}

signed long runroundCeilHundredth(signed long d) {
    return ((d % 100) != 0) ? d - (d % 100) + 100 : d;
}

signed long runroundFloorHundredth(signed long d) {
    return ((d % 100) != 0) ? d - (d % 100) - 100 : d;
}

void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume) {

    // hedge order when order is filled
    if (mAsks.count(clientOrderId) == 1) {
        mPosition -= (long)volume;
        SendHedgeOrder(mNextMessageId++, Side::BUY, MAX_ASK_NEAREST_TICK,volume);
    } else if (mBids.count(clientOrderId) == 1) {
        mPosition += (long)volume;
        SendHedgeOrder(mNextMessageId++, Side::SELL, MIN_BID_NEAREST_TICK,volume);
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

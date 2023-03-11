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
#include <array>

#include <boost/asio/io_context.hpp>

#include <ready_trader_go/logging.h>

#include "autotrader2.h"

#include <algorithm>
#include <iostream>
#include <deque>

using namespace ReadyTraderGo;

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

constexpr int LOT_SIZE = 5;
constexpr int POSITION_LIMIT = 100;
constexpr int TICK_SIZE_IN_CENTS = 100;

//custom
constexpr int MOVING_AVERAGE = 30;
constexpr int BUFFER = 20;
constexpr int TRANSACTION_LIMIT = 10;

double calcMean(std::deque<signed long> &data);
double calcStandardDev(std::deque<signed long> &data);
double calcStandardDev(std::deque<signed long> &X, std::deque<signed long> &Y);
AutoTrader::AutoTrader(boost::asio::io_context& context) : BaseAutoTrader(context)
{
    
}

void AutoTrader::DisconnectHandler()
{
    BaseAutoTrader::DisconnectHandler();
    RLOG(LG_AT, LogLevel::LL_INFO) << "execution connection lost";
}

void AutoTrader::ErrorMessageHandler(unsigned long clientOrderId,
                                     const std::string& errorMessage)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "error with order " << clientOrderId << ": " << errorMessage;
    if (clientOrderId != 0 && ((mAsks.count(clientOrderId) == 1) || (mBids.count(clientOrderId) == 1)))
    {
        OrderStatusMessageHandler(clientOrderId, 0, 0, 0);
    }
}

void AutoTrader::HedgeFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "hedge order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " average price in cents";
}

void AutoTrader::OrderBookMessageHandler(Instrument instrument,
                                         unsigned long sequenceNumber,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "order book received for " << instrument << " instrument"
                                   << ": ask prices: " << askPrices[0]
                                   << "; ask volumes: " << askVolumes[0]
                                   << "; bid prices: " << bidPrices[0]
                                   << "; bid volumes: " << bidVolumes[0];


    //finding price of etf and future by using mid-spread price
    if (instrument == Instrument::FUTURE)
        lastPriceFuture = (askPrices[0] + bidPrices[0]) / 2;
    if (instrument == Instrument::ETF)
        lastPriceETF= (askPrices[0] + bidPrices[0]) / 2;


    if (lastPriceETF != 0 && lastPriceFuture != 0){
        signed long gap = lastPriceFuture - lastPriceETF;

        
        //if future is overvalued, buy ETF
        if (gaps.size() >= MOVING_AVERAGE && gap > avgGap + sdGap){
            signed long volume = long(ceil(abs(gap -  avgGap)/ sdGap));
            if (instrument == Instrument::ETF && mPosition + TRANSACTION_LIMIT < POSITION_LIMIT - BUFFER){
                volume = std::min(long(TRANSACTION_LIMIT), volume);
                unsigned long price = askPrices[0] * (1+volume/100);
                //std::cout << "buy " << volume << std::endl;
                SendInsertOrder(++mNextNextMessageId, Side::BUY, price, volume , Lifespan::FILL_AND_KILL);
                mBids.emplace(mNextNextMessageId);
            }
                
        }

        //if future is undervalued, sell ETF
        else if (gaps.size() >= MOVING_AVERAGE && gap < avgGap - sdGap){
            signed long volume = long(ceil(abs(gap -  avgGap) /sdGap));
            if (instrument == Instrument::ETF && mPosition -  TRANSACTION_LIMIT > -POSITION_LIMIT + BUFFER){
                volume = std::min(long(TRANSACTION_LIMIT), volume);
                unsigned long price = bidPrices[0] * (1-volume/100);
                //std::cout << "sell " << volume << std::endl;
                SendInsertOrder(++mNextNextMessageId, Side::SELL, price, volume, Lifespan::FILL_AND_KILL);
                mAsks.emplace(mNextNextMessageId);
            }
                
        }

        //updating avgGap & sdGap
        gaps.push_back(gap);
        ETFPrices.push_back(lastPriceETF);
        FuturePrices.push_back(lastPriceFuture);

        if (gaps.size() >= MOVING_AVERAGE){
            if (gaps.size() > MOVING_AVERAGE){
                gaps.pop_front();
                ETFPrices.pop_front();
                FuturePrices.pop_front();
            }
            avgGap = calcMean(gaps);
            sdGap = calcStandardDev(ETFPrices, FuturePrices);
        }
    }
}

//find s.d. of correlated variable of X-Y
double calcStandardDev(std::deque<signed long> &X, std::deque<signed long> &Y){
    double sX = calcStandardDev(X);
    double sY = calcStandardDev(Y);
    return sqrt(pow(sX,2) + pow(sY,2) - (2 * sX * sY)); //assume pearson correlation to be 1
}

double calcStandardDev(std::deque<signed long> &data){
    double res = 0;
    double mean = calcMean(data);
    for(int i = 0; i < data.size(); ++i) {
        res += pow(double(data[i]) - mean, 2);
    }

    return sqrt(res / data.size()-1);
}

double calcMean(std::deque<signed long> &data){
    signed long summ = 0;
    for (int i=0; i<data.size(); i++){
        summ += data[i];
    }
    return double(summ) / (data.size());

}

void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " cents";
    if (mAsks.count(clientOrderId) == 1)
    {
        mPosition -= (long)volume;
    }
    else if (mBids.count(clientOrderId) == 1)
    {
        mPosition += (long)volume;
    }
}

void AutoTrader::OrderStatusMessageHandler(unsigned long clientOrderId,
                                           unsigned long fillVolume,
                                           unsigned long remainingVolume,
                                           signed long fees)
{
    if (remainingVolume == 0)
    {
        if (clientOrderId == mAskId)
        {
            mAskId = 0;
        }
        else if (clientOrderId == mBidId)
        {
            mBidId = 0;
        }

        mAsks.erase(clientOrderId);
        mBids.erase(clientOrderId);
    }
}

void AutoTrader::TradeTicksMessageHandler(Instrument instrument,
                                          unsigned long sequenceNumber,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "trade ticks received for " << instrument << " instrument"
                                   << ": ask prices: " << askPrices[0]
                                   << "; ask volumes: " << askVolumes[0]
                                   << "; bid prices: " << bidPrices[0]
                                   << "; bid volumes: " << bidVolumes[0];
}

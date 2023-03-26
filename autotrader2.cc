#include <ready_trader_go/logging.h>

#include <array>
#include <boost/asio/io_context.hpp>
#include <iostream>
#include <deque>

#include "autotrader2.h"

using namespace ReadyTraderGo;

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

constexpr int POSITION_LIMIT = 100;
constexpr int TICK_SIZE_IN_CENTS = 100;
constexpr unsigned long MIN_BID_NEAREST_TICK = (MINIMUM_BID + TICK_SIZE_IN_CENTS) / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;
constexpr unsigned long MAX_ASK_NEAREST_TICK = MAXIMUM_ASK / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;

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
    
    if (mAsks.count(clientOrderId)){
        std::cout << "Hedge bought at " << price << std::endl;
        mPosition += volume;
        lastBuyPrice = price;
    }
    else if (mBids.count(clientOrderId)){
        std::cout << "Hedge sold at " << price << std::endl;
        std::cout << "Profit: " <<  (double) price - (double) lastBuyPrice << std::endl <<std::endl;
        mPosition -= volume;
    }
}

void AutoTrader::OrderBookMessageHandler(
    Instrument instrument, unsigned long sequenceNumber,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes) {

    if(instrument == Instrument::FUTURE){

        unsigned long askPrice = askPrices[0];
        unsigned long bidPrice = bidPrices[0];
        
        AddEntry(bidPrice, askPrice);

        InsertOrderWhenPossibleToSell(bidPrice);
        InsertOrderWhenPossibleToBuy(askPrice);
        
    }   
}



void AutoTrader::InsertOrderWhenPossibleToSell(unsigned long bidPrice){
    if (OpenSell(bidPrice) || CloseBuy(bidPrice)){
        Sell(MIN_BID_NEAREST_TICK, 100, Lifespan::FILL_AND_KILL);
    }

}

void AutoTrader::InsertOrderWhenPossibleToBuy(unsigned long askPrice){
    if (OpenBuy(askPrice)|| CloseSell(askPrice)){
        Buy(MAX_ASK_NEAREST_TICK, 100, Lifespan::FILL_AND_KILL);
    }
}

void AutoTrader::Buy(unsigned long askPrice, unsigned long volume, ReadyTraderGo::Lifespan lifespan){
    SendHedgeOrder(++mNextMessageId, Side::BUY, askPrice,volume);
    mAsks.emplace(mNextMessageId);
}

void AutoTrader::Sell(unsigned long bidPrice, unsigned long volume, ReadyTraderGo::Lifespan lifespan){
    SendHedgeOrder(++mNextMessageId, Side::SELL, bidPrice,volume);
    mBids.emplace(mNextMessageId);
}

void AutoTrader::AddEntry(unsigned long buyPrice, unsigned long sellPrice){
    if (buyPrices.size() > 52){
        buyPrices.pop_front();
        sellPrices.pop_front();
    }
    buyPrices.push_back(buyPrice);
    sellPrices.push_back(sellPrice);
        
}

bool AutoTrader::OpenBuy(unsigned long buyPrice){
    if (buyPrices.size() < 52)
        return false;
    unsigned long conversionLine = CalcConversionLine(buyPrices);
    unsigned long baseLine = CalcBaseLine(buyPrices);
    unsigned long cloudPoint = GetCloudTop(conversionLine, baseLine, buyPrices);
    if (IsAboveCloud(cloudPoint, buyPrice) && conversionLine > baseLine && mPosition == 0){
        // std::cout<<"Open buy position"<<std::endl;
        stopLoss = baseLine;
        return true;
    }
    return false;
}

bool AutoTrader::CloseBuy(unsigned long sellPrice){
    if (sellPrices.size() < 52 || mPosition <= 0)
        return false;
    unsigned long conversionLine = CalcConversionLine(sellPrices);
    unsigned long baseLine = CalcBaseLine(sellPrices);
    if (conversionLine < baseLine || sellPrice <= stopLoss){                   
        return true;
    }
    return false;
}

bool AutoTrader::OpenSell(unsigned long sellPrice){
    if (sellPrices.size() < 52)
        return false;
    unsigned long conversionLine = CalcConversionLine(sellPrices);
    unsigned long baseLine = CalcBaseLine(sellPrices);
    unsigned long cloudPoint = GetCloudBottom(conversionLine, baseLine, sellPrices);
    if (IsBelowCloud(cloudPoint, sellPrice) && conversionLine < baseLine && mPosition == 0){
        stopLoss = baseLine;
        return true;
    }
    return false;
}

bool AutoTrader::CloseSell(unsigned long buyPrice){
    if (buyPrices.size() < 52 || mPosition >= 0)
        return false;
    unsigned long conversionLine = CalcConversionLine(buyPrices);
    unsigned long baseLine = CalcBaseLine(buyPrices);
    if (conversionLine > baseLine || buyPrice >= stopLoss){                      
        return true;
    }
    return false;
}
               
unsigned long AutoTrader::CalcConversionLine(std::deque<unsigned long> &prices){
    unsigned long highest = prices[prices.size()-9];
    unsigned long lowest = highest;
    for (int i = prices.size()-9; i<prices.size(); i++){
        highest = std::max(highest, prices[i]);
        lowest = std::min(lowest, prices[i]);
    }
    return (highest+lowest)/2;
}

unsigned long AutoTrader::CalcBaseLine(std::deque<unsigned long> &prices){
    unsigned long highest = prices[prices.size()-27];
    unsigned long lowest = highest;
    for (int i = prices.size()-27; i<prices.size(); i++){
        highest = std::max(highest, prices[i]);
        lowest = std::min(lowest, prices[i]);
    }
    return (highest+lowest)/2;
}

unsigned long AutoTrader::CalcLeadingSpanA(unsigned long conversionLinePrice, unsigned long baseLinePrice){
    return (conversionLinePrice + baseLinePrice) / 2; 
}

unsigned long AutoTrader::CalcLeadingSpanB(std::deque<unsigned long> &prices){
    unsigned long highest = prices[prices.size()-52];
    unsigned long lowest = highest;
    for (int i = prices.size()-52; i<prices.size(); i++){
        highest = std::max(highest, prices[i]);
        lowest = std::min(lowest, prices[i]);
    }
    return (highest+lowest)/2;
}

unsigned long AutoTrader::GetCloudTop(unsigned long conversionLinePrice, unsigned long baseLinePrice, std::deque<unsigned long> &prices){
    unsigned long leadingSpanA = CalcLeadingSpanA(conversionLinePrice, baseLinePrice);
    unsigned long leadingSpanB = CalcLeadingSpanB(prices);
    return std::max(leadingSpanA, leadingSpanB);
}

unsigned long AutoTrader::GetCloudBottom(unsigned long conversionLinePrice, unsigned long baseLinePrice, std::deque<unsigned long> &prices){
    unsigned long leadingSpanA = CalcLeadingSpanA(conversionLinePrice, baseLinePrice);
    unsigned long leadingSpanB = CalcLeadingSpanB(prices);
    return std::min(leadingSpanA, leadingSpanB);
}

unsigned long AutoTrader::IsAboveCloud(unsigned long cloudPrice, unsigned long currentAskPrice){
    return (currentAskPrice > cloudPrice);
}

unsigned long AutoTrader::IsBelowCloud(unsigned long cloudPrice, unsigned long currentBidPrice){
    return (currentBidPrice < cloudPrice);
}

void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume) {

    // hedge order when order is filled
    std::cout << "Order filled at " << price << std::endl;
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

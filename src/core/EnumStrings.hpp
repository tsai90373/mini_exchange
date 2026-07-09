#pragma once
#include "Types.hpp"

// Canonical enum → string vocabulary，wire (PythonOrderAdapter) 與
// log (FileLogSubscriber) 共用。原本是 PythonOrderAdapter.cpp 的
// file-local helpers，出現第二個消費者後 hoist 到 core。
//
// 字串值即 wire_format.md § 4 的 wire 值 — 改字串 = 改 wire protocol。

inline const char* ToString(Side s) {
    return s == Side::kSell ? "Sell" : "Buy";
}

inline const char* ToString(OpType o) {
    switch (o) {
        case OpType::New:         return "New";
        case OpType::Cancel:      return "Cancel";
        case OpType::UpdatePrice: return "UpdatePrice";
        case OpType::UpdateQty:   return "UpdateQty";
        case OpType::Unknown:     return "Unknown";
    }
    return "Unknown";
}

inline const char* ToString(PriceType p) {
    switch (p) {
        case PriceType::LMT:     return "LMT";
        case PriceType::MKT:     return "MKT";
        case PriceType::MKP:     return "MKP";
        case PriceType::Unknown: return "Unknown";
    }
    return "Unknown";
}

inline const char* ToString(OrderType o) {
    switch (o) {
        case OrderType::ROD:     return "ROD";
        case OrderType::IOC:     return "IOC";
        case OrderType::FOK:     return "FOK";
        case OrderType::Unknown: return "Unknown";
    }
    return "Unknown";
}

inline const char* ToString(OCType o) {
    switch (o) {
        case OCType::Auto:     return "Auto";
        case OCType::New:      return "New";
        case OCType::Cover:    return "Cover";
        case OCType::DayTrade: return "DayTrade";
        case OCType::Unknown:  return "Unknown";
    }
    return "Unknown";
}

// wire_format.md § 4：account 走小寫，對齊 Shioaji 序列化字串。
inline const char* ToString(Account a) {
    switch (a) {
        case Account::Stock:   return "stock";
        case Account::FutOpt:  return "futopt";
        case Account::Unknown: return "Unknown";
    }
    return "Unknown";
}

// 不在 wire 上（wire 沒有 ord_st 欄位），目前只給 log。
inline const char* ToString(OrdSt st) {
    switch (st) {
        case OrdSt::FAILED:         return "Failed";
        case OrdSt::CANCELLED:      return "Cancelled";
        case OrdSt::SENDING:        return "Sending";
        case OrdSt::NEWASK:         return "NewAsk";
        case OrdSt::PARTIAL_FILLED: return "PartialFilled";
        case OrdSt::FULL_FILLED:    return "FullFilled";
    }
    return "Unknown";
}

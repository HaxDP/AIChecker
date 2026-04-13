#pragma once

#include <string>
#include <vector>

namespace core {

class IObserver {
public:
    virtual ~IObserver() = default;
    virtual void OnEvent(const std::string& text) = 0;
};

class EventBus {
public:
    void Subscribe(IObserver* observer) {
        observers_.push_back(observer);
    }

    void Emit(const std::string& message) {
        for (auto* observer : observers_) {
            observer->OnEvent(message);
        }
    }

private:
    std::vector<IObserver*> observers_;
};

}
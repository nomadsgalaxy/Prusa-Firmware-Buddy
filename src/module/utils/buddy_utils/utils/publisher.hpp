/// \file
#pragma once

#include <inplace_function.hpp>

#include <utils/uncopyable.hpp>

template <typename... Args>
class Subscriber;

/// Point for registering callbacks to
/// Represented as a linked list
/// !!! Not thread-safe
template <typename... Args>
class PublisherBase : Uncopyable {

public:
    using Subscriber = ::Subscriber<Args...>;
    friend Subscriber;

protected:
    /// Calls callbacks of all registered Subscribers
    /// The execution order depends on the insertion order - newer hooks execute first.
    /// Warning - if a hook removes itself during the call, it can cause UB or crash.
    void call_all(Args &&...args) {
        for (auto it = first_; it; it = it->next_) {
            it->callback_(std::forward<Args>(args)...);
        }
    }

private:
    void insert(Subscriber *item) {
        item->next_ = first_;
        first_ = item;
    }

    void remove(Subscriber *item) {
        Subscriber **current = &first_;
        while (*current != item) {
            assert(*current);
            current = &((*current)->next_);
        }
        *current = (*current)->next_;
    }

private:
    Subscriber *first_ = nullptr;
};

template <typename... Args>
class Publisher : public PublisherBase<Args...> {

public:
    using PublisherBase<Args...>::call_all;
};

/// Guard that registers the provided callback to the specified point
/// The hook gets removed when the function is destroyed
/// !!! Not thread safe
template <typename... Args>
class Subscriber : Uncopyable {

public:
    using Callback = stdext::inplace_function<void(Args...)>;
    using Publisher = ::PublisherBase<Args...>;
    friend Publisher;

public:
    Subscriber() {
    }

    Subscriber(const auto &callback) {
        set_callback(callback);
    }

    // Note: Template deducation problems without the "auto"
    Subscriber(Publisher &publisher, const auto &callback) {
        set_callback(callback);
        bind(publisher);
    }

    ~Subscriber() {
        unbind();
    }

public:
    void bind(Publisher &publisher) {
        unbind();

        assert(callback_);
        publisher.insert(this);
        publisher_ = &publisher;
    }

    void unbind() {
        if (publisher_) {
            publisher_->remove(this);
            publisher_ = nullptr;
        }
    }

    // Note: Template deducation problems without the "auto"
    void set_callback(const auto &callback) {
        callback_ = callback;
    }

private:
    Publisher *publisher_ = nullptr;
    Subscriber *next_ = nullptr;
    Callback callback_ = {};
};

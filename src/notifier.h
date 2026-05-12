#pragma once
#include <string>

// Send a native OS notification.
// Returns true on success. Never throws.
bool sendNotification(const std::string& title, const std::string& message);

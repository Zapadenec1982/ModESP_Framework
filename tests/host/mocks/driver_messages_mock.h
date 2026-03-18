/**
 * @file driver_messages_mock.h
 * @brief HOST BUILD: empty message ID stubs.
 */
#pragma once

#ifndef DRIVER_MESSAGES_MOCK_H
#define DRIVER_MESSAGES_MOCK_H

// Empty message type stubs — modules don't send messages in host tests
struct DriverMessage {
    int id = 0;
};

#endif // DRIVER_MESSAGES_MOCK_H

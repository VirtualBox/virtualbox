/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#pragma once

#include <string>
#include <exception>

namespace dxmt {

/**
 * \brief DXMT error
 *
 * A generic exception class that stores a
 * message. Exceptions should be logged.
 */
class MTLD3DError : public std::exception {

public:
  MTLD3DError() {}
  MTLD3DError(std::string &&message) : m_message(std::move(message)) {}

  const std::string &message() const { return m_message; }

  const char *what() const noexcept { return m_message.c_str(); }

private:
  std::string m_message;
};

} // namespace dxmt
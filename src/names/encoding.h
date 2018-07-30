// Copyright (c) 2018 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_NAMES_ENCODING
#define H_BITCOIN_NAMES_ENCODING

#include <script/script.h>

#include <stdexcept>
#include <string>

/**
 * Enum for the possible encodings of names/values in the RPC interface.
 */
enum class NameEncoding
{
  /**
   * Only printable ASCII characters (code in [0x20, 0x80)) are allowed.
   */
  ASCII,

  /**
   * Valid UTF-8 with printable characters (code >=0x20).
   */
  UTF8,

  /**
   * Hex-encoded arbitrary binary data.
   */
  HEX,
};

/* Utility functions to convert encodings to/from the enum.  They throw
   std::invalid_argument if the conversion fails.  */
NameEncoding EncodingFromString (const std::string& str);
std::string EncodingToString (NameEncoding enc);

/**
 * Exception that is thrown if a name/value string is invalid according
 * to the chosen encoding.
 */
class InvalidNameString : public std::invalid_argument
{

public:

  InvalidNameString () = delete;

  InvalidNameString (NameEncoding enc, const std::string& invalidStr);

};

/**
 * Encodes a name or value to a string with the given encoding.  Throws
 * InvalidNameString if the data is not valid for the encoding.
 */
std::string EncodeName (const valtype& data, NameEncoding enc);

/**
 * Decodes a string to a raw name/value.  Throws InvalidNameString
 * if the string is invalid for the requested encoding.
 */
valtype DecodeName (const std::string& str, NameEncoding enc);

#endif // H_BITCOIN_NAMES_ENCODING

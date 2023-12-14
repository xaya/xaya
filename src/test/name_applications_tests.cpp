// Copyright (c) 2011-2020 The Bitcoin Core developers
// Copyright (c) 2022 The Namecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <names/applications.h>
#include <names/encoding.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(name_applications_tests)

BOOST_AUTO_TEST_CASE( namespace_detection )
{
    const valtype noSlash = DecodeName ("wikileaks", NameEncoding::ASCII);
    BOOST_CHECK(NamespaceFromName(noSlash) == NameNamespace::NonStandard);

    const valtype weirdNamespace = DecodeName ("nft/wikileaks", NameEncoding::ASCII);
    BOOST_CHECK(NamespaceFromName(weirdNamespace) == NameNamespace::NonStandard);

    const valtype empty = DecodeName ("", NameEncoding::ASCII);
    BOOST_CHECK(NamespaceFromName(empty) == NameNamespace::NonStandard);

    const valtype playerValid = DecodeName ("p/wikileaks", NameEncoding::ASCII);
    BOOST_CHECK(NamespaceFromName(playerValid) == NameNamespace::Player);

    const valtype gameValid = DecodeName ("g/wikileaks", NameEncoding::ASCII);
    BOOST_CHECK(NamespaceFromName(gameValid) == NameNamespace::Game);

    const valtype utf8Valid = DecodeName (reinterpret_cast<const char*> (u8"p/äöü"), NameEncoding::UTF8);
    BOOST_CHECK(NamespaceFromName(utf8Valid) == NameNamespace::Player);
}

BOOST_AUTO_TEST_CASE( name_description )
{
    const valtype playerValid = DecodeName ("p/wikileaks", NameEncoding::ASCII);
    BOOST_CHECK(DescFromName(playerValid, NameNamespace::Player) == "'p/wikileaks'");
}

BOOST_AUTO_TEST_SUITE_END()

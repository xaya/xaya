// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/uritests.h>

#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <QUrl>

void URITests::uriTests()
{
    SendCoinsRecipient rv;
    QUrl uri;
    uri.setUrl(QString("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?req-dontexist="));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?dontexist="));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?label=Wikipedia Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT"));
    QVERIFY(rv.label == QString("Wikipedia Example Address"));
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?amount=0.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100000);

    uri.setUrl(QString("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?amount=1.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100100000);

    uri.setUrl(QString("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?amount=100&label=Wikipedia Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Wikipedia Example"));

    uri.setUrl(QString("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?message=Wikipedia Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT"));
    QVERIFY(rv.label == QString());

    QVERIFY(GUIUtil::parseBitcoinURI("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?message=Wikipedia Example Address", &rv));
    QVERIFY(rv.address == QString("CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT"));
    QVERIFY(rv.label == QString());

    uri.setUrl(QString("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?req-message=Wikipedia Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?amount=1,000&label=Wikipedia Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("xyon:CJDPZBVLi6tx2mST1Z4BSANNeztHunz9LT?amount=1,000.0&label=Wikipedia Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));
}

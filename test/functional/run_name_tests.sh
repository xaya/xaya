#!/bin/sh

echo "\nName and value encodings..."
./name_encodings.py

echo "\nName expiration..."
./name_expiration.py

echo "\nName with immature inputs..."
./name_immature_inputs.py

echo "\nName ismine field..."
./name_ismine.py

echo "\nName list..."
./name_list.py

echo "\nName listunspent..."
./name_listunspent.py

echo "\nName multisig..."
./name_multisig.py

echo "\nName multiupdates..."
./name_multiupdate.py

echo "\nName pending..."
./name_pending.py

echo "\nName rawtx operations..."
./name_rawtx.py

echo "\nName registration..."
./name_registration.py

echo "\nName reorgs..."
./name_reorg.py

echo "\nName scanning..."
./name_scanning.py

echo "\nNames and segwit..."
./name_segwit.py

echo "\nName operation with sendCoins..."
./name_sendcoins.py

echo "\nNames and the UTXO set..."
./name_utxo.py

echo "\nName wallet..."
./name_wallet.py

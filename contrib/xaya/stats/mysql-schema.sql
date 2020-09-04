--  Xaya Stats - extract Xaya blockchain statistics
--  Copyright (C) 2020  The Xaya developers
--
--  This program is free software: you can redistribute it and/or modify
--  it under the terms of the GNU Affero General Public License as published by
--  the Free Software Foundation, either version 3 of the License, or
--  (at your option) any later version.
--
--  This program is distributed in the hope that it will be useful,
--  but WITHOUT ANY WARRANTY; without even the implied warranty of
--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--  GNU Affero General Public License for more details.
--
--  You should have received a copy of the GNU Affero General Public License
--  along with this program.  If not, see <http://www.gnu.org/licenses/>.

-- Database schema for MySQL so that the update-mysql.py script can
-- keep and refresh stats in a database.

CREATE TABLE `blocks` (
  `blockhash` CHAR(64) NOT NULL,
  `height` INTEGER NOT NULL,
  `timestamp` INTEGER NOT NULL,
  `size` INTEGER NOT NULL,
  `ntx` INTEGER NOT NULL,
  PRIMARY KEY (`blockhash`)
);

CREATE TABLE `names` (
  `blockhash` CHAR(64) NOT NULL,
  `height` INTEGER NOT NULL,
  `name` VARCHAR(255) BINARY NOT NULL,
  `registered` INTEGER NOT NULL,
  `updates` INTEGER NOT NULL,
  PRIMARY KEY (`blockhash`, `name`)
);

CREATE TABLE `games` (
  `blockhash` CHAR(64) NOT NULL,
  `height` INTEGER NOT NULL,
  `game` VARCHAR(255) BINARY NOT NULL,
  `name` VARCHAR(255) BINARY NOT NULL,
  `moves` INTEGER NOT NULL,
  PRIMARY KEY (`blockhash`, `game`, `name`)
);

<?php
/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/


namespace Modules\RSM\Helpers;

use API;

class ValueMapHelper {

    /** @var array $value_map  Cached value map arrays. Key is value map name. */
    static public $value_map = [];

    /**
     * Get mapped value according $value_map definition.
     *
     * @param string $value_map    Value mapping name.
     * @param string $key          Key to search in value mapping.
     */
    static public function get(string $value_map, ?string $key) {
        self::getMapping($value_map);

        return self::$value_map[$value_map][$key] ?? $key;
    }

    /**
     * Get value mapping array.
     *
     * @param string $value_map    Value mapping name.
     */
    static public function getMapping(string $value_map): ?array {
        if (!isset(self::$value_map[$value_map])) {
            $vmap = API::ValueMap()->get([
                'output' => ['name'],
                'filter' => ['name' => $value_map],
                'selectMappings' => 'extend'
            ]);
            self::$value_map[$value_map] = array_column(reset($vmap)['mappings'] ?? [], 'newvalue', 'value');
        }

        return self::$value_map[$value_map];
    }
}

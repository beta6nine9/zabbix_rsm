<?php

namespace Modules\RSM\Helpers;

use CTableInfo as Base;
use CTag;

class CTableInfo extends Base {
	public function setMultirowHeader($row_items = null, $column_count = 1) {
		$row_items = new CTag('thead', true, $row_items);
		$this->header = $row_items->toString();
		$this->colnum = $column_count;

		return $this;
	}
}

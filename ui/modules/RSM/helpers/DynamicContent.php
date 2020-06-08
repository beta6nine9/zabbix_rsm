<?php

namespace Modules\RSM\Helpers;

use CDiv;
use CScriptTag;

class DynamicContent extends CDiv {
	/** @var int|null $refresh_seconds   Auto update content after seconds, null - do not update. */
	public $refresh_seconds = null;

	/** @var bool $listen_timeselector   Update content on time selector update. */
	public $listen_timeselector = true;

	protected function getJS() {
		$id = $this->getAttribute('id');
		$autorefresh = ($this->refresh_seconds === null)
			? ''
			: 'clearTimeout(timer);timer = setTimeout(refresh, '.($this->refresh_seconds*1000).');';
		$js = 'var shadow_container = $(\'<script type="text/ajax" style="display:none"/>\'),'.
				'timer = null;'.
			$autorefresh.
			'function refresh() {'.
				'$.get(window.location.href).fail(function() {'.
					'$("#'.$id.'").text("Cannot load data, please refresh page.");'.
				'}).done(function(resp) {'.
					'shadow_container.html(resp);'.
					'$("#'.$id.'").replaceWith(shadow_container.find("#'.$id.'"));'.
					$autorefresh.
				'});'.
			'}';

		if ($this->listen_timeselector) {
			$js .= '$.subscribe("timeselector.rangeupdate", refresh);';
		}

		return new CScriptTag($js);
	}

	public function toString($destroy = true) {
		return parent::toString() . ($this->getJS()->setOnDocumentReady()->toString());
	}
}

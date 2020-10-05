<?php

namespace Modules\RSM\Services;

use API;

class MacroService {

	protected $cache = [];

	/**
	 * Get macro value.
	 *
	 * @param string $macro      Macro name.
	 * @return string|null
	 */
	public function get($macro) {
		if (!array_key_exists($macro, $this->cache)) {
			$this->read([$macro]);
		}

		if (array_key_exists($macro, $this->cache)) {
			return $this->cache[$macro];
		}

		return null;
	}

	/**
	 * Read macro values from database to local $cache array. For non existing macro error will be shown.
	 *
	 * @param array $macro_keys    Array of desired global macro to be read.
	 */
	public function read(array $macro_keys) {
		$macros = API::UserMacro()->get([
			'output' => ['macro', 'value'],
			'filter' => [
				'macro' => $macro_keys
			],
			'globalmacro' => true
		]);

		if (!is_array($macros)) {
			return false;
		}

		$macro_keys = array_combine($macro_keys, $macro_keys);

		foreach ($macros as $macro) {
			$this->cache[$macro['macro']] = $macro['value'];
			$macro_keys[$macro['macro']] = '';
		}

		foreach (array_filter($macro_keys) as $macro) {
			error(_s('Macro "%1$s" not exist.', $macro));
		}
	}
}

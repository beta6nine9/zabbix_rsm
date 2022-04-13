<?php
/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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

require_once dirname(__FILE__).'/../../include/CWebTest.php';

/**
 * @backup profiles
 */
class testRSM extends CWebTest {

	const DATES = [
		'id:from' => '2022-02-01 00:00:00',
		'id:to' => '2022-03-31 00:00:00'
	];

	const FILTER_CHECKBOXES = [
		'Services' => ['id:filter_dns', 'id:filter_dnssec', 'id:filter_rdds', 'id:filter_epp'],
		'TLD types' => ['id:filter_cctld_group', 'id:filter_gtld_group', 'id:filter_othertld_group', 'id:filter_test_group'],
		'Enabled subservices' => ['id:filter_rdds43_subgroup', 'id:filter_rdds80_subgroup', 'id:filter_rdap_subgroup']
	];

	public static function getRollingWeekPageData() {
		return [
			[
				[
					'case' => 'empty_filter',
					'filter_checkboxes' => false
				]
			],
			[
				[
					'case' => 'filter_with_data',
					'filter_checkboxes' => true
				]
			]
		];
	}

	/**
	 * @dataProvider getRollingWeekPageData
	 */
	public function testRSM_RollingWeekFilter($data) {
		$this->page->login()->open('zabbix.php?action=rsm.rollingweekstatus')->waitUntilReady();
		$form = $this->query('name:zbx_filter')->asForm()->waitUntilVisible()->one();

		foreach (self::FILTER_CHECKBOXES as $name => $checkboxes) {
			foreach ($checkboxes as $checkbox) {
				$form->fill([$checkbox => $data['filter_checkboxes']]);
			}
		}

		$form->submit();
		$this->assertScreenshot($this->query('class:list-table')->waitUntilVisible()->one(),
				$data['case'].' '.$name.' '.$data['filter_checkboxes']
		);
	}

	public static function getRollingWeekIncidentsGraphsData() {
		return [
			[
				[
					'column' => 'DNS (4Hrs)',
					'tab_id' => 'dnsTab',
					'find' => '%'
				]
			],
			[
				[
					'column' => 'DNSSEC (4Hrs)',
					'tab_id' => 'dnssecTab',
					'find' => '%'
				]
			],
			[
				[
					'column' => 'RDDS (24Hrs)',
					'tab_id' => 'rddsTab',
					'find' => '%'
				]
			],
			[
				[
					'column' => 'DNS (4Hrs)',
					'find' => 'graph',
					'header' => 'DNS weekly unavailability'
				]
			],
			[
				[
					'column' => 'DNSSEC (4Hrs)',
					'find' => 'graph',
					'header' => 'DNSSEC weekly unavailability'
				]
			],
			[
				[
					'column' => 'RDDS (24Hrs)',
					'find' => 'graph',
					'header' => 'RDDS weekly unavailability'
				]
			]
		];
	}

	/**
	 * @dataProvider getRollingWeekIncidentsGraphsData
	 */
	public function testRSM_RollingWeekIncidentsGraphs($data) {
		$tld = 'tld105';

		$this->page->login()->open('zabbix.php?action=rsm.rollingweekstatus')->waitUntilReady();
		// This line is commented because of DEV-2112 (1).
//		$this->query('button:Reset')->waitUntilClickable()->one()->click();

		// Tick all checkboxes, because Reset filter does not work.
		$form = $this->query('name:zbx_filter')->asForm()->waitUntilVisible()->one();
		foreach (self::FILTER_CHECKBOXES as $name => $checkboxes) {
			foreach ($checkboxes as $checkbox) {
				$form->fill([$checkbox => true]);
			}
		}
		$form->submit();
		$this->page->waitUntilReady();

		// Click particular link from data: % or graph.
		$this->query('class:list-table')->asTable()->waitUntilVisible()->one()->findRow('TLD', $tld)
				->getColumn($data['column'])->query('xpath:.//a[contains(text(), '.
				CXPathHelper::escapeQuotes($data['find']).')]')->one()->click();
		$this->page->waitUntilReady();

		// Check the header of opened page.
		$this->page->assertHeader(($data['find'] === '%') ? 'Incidents' : $tld.': '.$data['header']);

		// Select date filter tab treshhold is checked.
		if ($data['find'] === '%') {
			$this->query('xpath://li[@tabindex="-1"]')->waitUntilClickable()->one()->click();
			$form->invalidate();
		}

		// Fill the necessary date period.
		$form->fill(self::DATES);
		$form->query('button:Apply')->waitUntilClickable()->one()->click();
		$this->page->waitUntilReady();

		// Take screenshot of Incidents detail page or Graph.
		$area = ($data['find'] === '%')
			? $this->query('id:incidents_data')->waitUntilVisible()->one()
			: $this->waitUntilGraphIsLoaded();

		$this->assertScreenshot($area,
				$data['column'].(($data['find'] === '%') ? ' TLD Rolling week status' : ' '.$data['header'].' graph')
		);

		// Click on Incident ID and take page screenshot.
		if ($data['find'] === '%') {
			$this->query('xpath://div[@id='.CXPathHelper::escapeQuotes($data['tab_id']).']//table[@class="list-table"]')
					->asTable()->waitUntilVisible()->one()->getRow(0)->getColumn('Incident ID')
					->query('tag:a')->waitUntilClickable()->one()->click();

			$this->page->assertHeader('Incidents details');
			$this->assertScreenshot($this->query('id:incident_details')->waitUntilVisible()->one(), $data['column'].' Incident ID');
		}
	}

	public static function getIncidentsTabsTestsData() {
		return [
			[
				[
					'tld' => 'tld6'
				]
			],
			[
				[
					'tld' => 'tld105'
				]
			]
		];
	}

	/**
	 * @dataProvider getIncidentsTabsTestsData
	 */
	public function testRSM_IncidentsTabsTests($data) {
		$filtered_link = 'zabbix.php?type=0&filter_set=1&filter_search='.$data['tld'].'&from=2022-02-01%2000%3A00%3A00&'.
				'to=2022-03-31%2000%3A00%3A00&action=rsm.incidents';
		$this->page->login()->open($filtered_link)->waitUntilReady();

		$tabs = ['DNS', 'DNSSEC', 'RDDS', 'EPP'];
		foreach ($tabs as $tab) {
			$this->query('link', $tab)->one()->waitUntilClickable()->click();
			$this->assertScreenshot($this->query('id:incidents_data')->waitUntilVisible()->one(), $data['tld'].' '.$tab.' Incident page');

			if ($tab !== 'EPP') {
				// Click on tests count in incident info block for each Tab.
				$this->query('xpath://div[@id='. CXPathHelper::escapeQuotes(strtolower($tab).'Tab').
						']//table[@class="incidents-info"]//a')->one()->waitUntilClickable()->click();
				$this->page->waitUntilReady();
				$this->page->assertHeader('Tests');
				$this->assertScreenshot($this->query('id:rsm_tests')->waitUntilVisible()->one(), $data['tld'].' '.$tab.' Tests page');

				// For tld6 DNSSEC tab there are no any tests.
				if ($data['tld'] !== 'tld6' || $tab !== 'DNSSEC') {
					// Click on first row test details.
					$this->query('xpath://table[@class="list-table"]')->asTable()->waitUntilVisible()->one()
							->getRow(0)->getColumn('')->query('link:Details')->waitUntilClickable()->one()->click();

					$this->page->assertHeader('Test details');
					$this->assertScreenshot(null, $data['tld'].' '.$tab.' Test details');
				}
			}

			// Return to Incident page with tabs.
			$this->page->open($filtered_link)->waitUntilReady();
		}
	}

	public static function getIncidentsIDTestData() {
		return [
			[
				[
					'tld' => 'tld6',
					'tab' => 'DNS',
					'row' => 0,
					'color' => 'green' //Green: Up-inconclusive-reconfig,
				]
			],
			[
				[
					'tld' => 'tld6',
					'tab' => 'RDDS',
					'row' => 3,
					'color' => 'red', // Red: Down
					'check_hints' =>	[
						['number' => '-201', 'title' => 'Whois server returned no NS'],
						['number' => '-349', 'title' => 'RDDS80 - Expecting HTTP status code 200 but got 500']
					]
				]
			],
			[
				[
					'tld' => 'tld105',
					'tab' => 'DNS',
					'row' => 0,
					'color' => 'green' // Green: Up
				]
			],
			[
				[
					'tld' => 'tld105',
					'tab' => 'DNSSEC',
					'row' => 4,
					'color' => 'red', // Red: Down
					'check_hints' => [
						['number' => '-405', 'title' => 'DNS UDP - Unknown cryptographic algorithm']
					]
				]
			]
		];
	}

	/**
	 * @dataProvider getIncidentsIDTestData
	 */
	public function testRSM_IncidentsIDTestDetails($data) {
		$this->page->login()->open('zabbix.php?type=0&filter_set=1&filter_search='.$data['tld'].'&from=2022-02-01%2000%3A00%3A00&'.
				'to=2022-03-31%2000%3A00%3A00&action=rsm.incidents')->waitUntilReady();

		// Click on necessary Tab.
		$this->query('link', $data['tab'])->one()->waitUntilClickable()->click();

		// Click on IncidentID in table.
		$this->query('xpath://div[@id='. CXPathHelper::escapeQuotes(strtolower($data['tab']).'Tab').
				']//table[@class="list-table"]')->asTable()->waitUntilVisible()->one()
				->getRow(0)->getColumn('Incident ID')->query('tag:a')->waitUntilClickable()->one()->click();

		$this->page->waitUntilReady();
		$this->page->assertHeader('Incidents details');
		$this->assertScreenshot($this->query('id:incident_details')->waitUntilVisible()->one(),
				$data['tld'].' '.$data['tab'].' Incidents details page'
		);

		// Click on Details link in a necessary row.
		$this->query('xpath://table[@class="list-table"]')->asTable()->waitUntilVisible()->one()->getRow($data['row'])
				->getColumn('')->query('link:Details')->waitUntilClickable()->one()->click();

		$this->page->waitUntilReady();
		$this->page->assertHeader('Test details');
		$this->assertScreenshot(null, $data['tld'].' '.$data['tab'].' Test details page '.$data['color']);

		// Check hints' texts on corresponding number.
		if (CTestArrayHelper::get($data, 'check_hints')) {
			foreach ($data['check_hints'] as $hint) {
				$this->assertTrue($this->query('xpath://span[@title='.CXPathHelper::escapeQuotes($hint['title']).
						' and text() ='.CXPathHelper::escapeQuotes($hint['number']).']')->exists()
				);
			}
		}
	}

	public static function getIncidentsDetailsFiltersData() {
		return [
			[
				[
					'id:filter_search' => 'tld6'
				]
			],
			[
				[
					'id:filter_search' => 'tld105'
				]
			]
		];
	}

	/**
	 * @dataProvider getIncidentsDetailsFiltersData
	 */
	public function testRSM_IncidentsDetailsFilters($data) {
		$this->page->login()->open('zabbix.php?type=0&filter_set=1&filter_search='.$data['tld'].
				'&from=2022-02-01%2000%3A00%3A00&to=2022-03-31%2000%3A00%3A00&action=rsm.incidents')->waitUntilReady();
		$this->page->assertHeader('Incidents');

		// Click on IncidentID in table.
		$this->query('xpath://div[@id="dnsTab"]//table[@class="list-table"]')->asTable()->waitUntilVisible()->one()
				->getRow(0)->getColumn('Incident ID')->query('tag:a')->waitUntilClickable()->one()->click();

		$this->page->waitUntilReady();
		$this->page->assertHeader('Incidents details');

		// Open filter tab if it is not opened.
		$selected = $this->query('xpath://li[@aria-controls="tab_2"]')->one()->getAttribute('aria-selected');
		if ($selected === "false") {
			$this->query('xpath://li[@tabindex="-1"]')->waitUntilClickable()->one()->click();
		}

		// Select necessary filter and take sceenshot of page.
		foreach (['Only failing tests', 'Show all'] as $filter) {
			$this->query('id:filter_failing_tests')->asSegmentedRadio()->waitUntilVisible()->one()->select($filter);
			$this->page->waitUntilReady();
			$this->assertScreenshot($this->query('id:incident_details')->waitUntilVisible()->one(), $data['id:filter_search'].' '.$filter);
		}
	}

	public static function getSLAData() {
		return [
			[
				[
					'TLD' => 'tld5',
					'name:filter_month' => 'March'
				]
			],
			[
				[
					'TLD' => 'tld5',
					'name:filter_month' => 'January'
				]
			],
			[
				[
					'TLD' => 'tld105',
					'name:filter_month' => 'March'
				]
			]
		];
	}

	/**
	 * @dataProvider getSLAData
	 */
	public function testRSM_SLAPage($data) {
		$this->page->login()->open('zabbix.php?action=rsm.slareports')->waitUntilReady();
		$form = $this->query('name:zbx_filter')->asForm()->waitUntilVisible()->one();
		$form->fill($data);
		$form->submit();
		$this->page->removeFocus();
		$this->assertScreenshot(null, $data['TLD'].$data['name:filter_month']);
	}

	/**
	 * Function for waiting loader ring.
	 */
	private function waitUntilGraphIsLoaded() {
		try {
			$this->query('xpath://div[contains(@class,"is-loading")]/img')->waitUntilPresent();
		}
		catch (\Exception $ex) {
			// Code is not missing here.
		}

		return $this->query('xpath://div[not(contains(@class,"is-loading"))]/img')->waitUntilPresent()->one();
	}
}

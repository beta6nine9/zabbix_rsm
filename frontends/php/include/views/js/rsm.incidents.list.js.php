<script type="text/javascript">
	jQuery(function() {
		if (jQuery('#filter_search').length) {
				createSuggest('filter_search', true);
		}
	});

	function rollingweek () {
		var tld = jQuery('#filter_search').val();
		location.href = 'rsm.incidents.php?incident_type=<?= $this->data['type'] ?>&filter_set=1&filter_search=' + tld + '&filter_rolling_week=1&sid=';
	}
</script>

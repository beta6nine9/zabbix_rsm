<script type="text/javascript">
	jQuery(function() {
		jQuery('#checkAllServices').on('click', function() {
			if (jQuery('#checkAllServicesValue').val() == 0) {
				jQuery('#filter_dns').prop('checked', true);
				jQuery('#filter_dnssec').prop('checked', true);
				jQuery('#filter_rdds').prop('checked', true);
				jQuery('#filter_rdap').prop('checked', true);
				jQuery('#filter_epp').prop('checked', true);
				jQuery('#checkAllServicesValue').val(1);
			}
			else {
				jQuery('#filter_dns').prop('checked', false);
				jQuery('#filter_dnssec').prop('checked', false);
				jQuery('#filter_rdds').prop('checked', false);
				jQuery('#filter_rdap').prop('checked', false);
				jQuery('#filter_epp').prop('checked', false);
				jQuery('#checkAllServicesValue').val(0);
			}
		});

		jQuery('#checkAllSubservices').on('click', function() {
			if (jQuery('#checkAllSubservicesValue').val() == 0) {
				jQuery('#filter_rdap_subgroup').prop('checked', true);
				jQuery('#filter_rdds43_subgroup').prop('checked', true);
				jQuery('#filter_rdds80_subgroup').prop('checked', true);
				jQuery('#checkAllSubservicesValue').val(1);
			}
			else {
				jQuery('#filter_rdap_subgroup').prop('checked', false);
				jQuery('#filter_rdds43_subgroup').prop('checked', false);
				jQuery('#filter_rdds80_subgroup').prop('checked', false);
				jQuery('#checkAllSubservicesValue').val(0);
			}
		});

		jQuery('#checkAllGroups').on('click', function() {
			if (jQuery('#checkAllGroupsValue').val() == 0) {
				if (!jQuery('#filter_cctld_group').prop('disabled')) {
					jQuery('#filter_cctld_group').prop('checked', true);
				}
				if (!jQuery('#filter_gtld_group').prop('disabled')) {
					jQuery('#filter_gtld_group').prop('checked', true);
				}
				if (!jQuery('#filter_othertld_group').prop('disabled')) {
					jQuery('#filter_othertld_group').prop('checked', true);
				}
				if (!jQuery('#filter_test_group').prop('disabled')) {
					jQuery('#filter_test_group').prop('checked', true);
				}
				jQuery('#checkAllGroupsValue').val(1);
			}
			else {
				jQuery('#filter_cctld_group').prop('checked', false);
				jQuery('#filter_gtld_group').prop('checked', false);
				jQuery('#filter_othertld_group').prop('checked', false);
				jQuery('#filter_test_group').prop('checked', false);
				jQuery('#checkAllGroupsValue').val(0);
			}
		});
	});
</script>

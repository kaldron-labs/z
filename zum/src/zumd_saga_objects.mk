# Shared production saga objects for unit and integration tests.
zumd_saga_names = \
	enrollment credential_add recovery_start recovery_enroll code_family \
	app_enrollment external_projection app_action_add assignment_change \
	role_delete catalog_publish assignment_add role_edit action_edit app_change \
	user_edit cred_edit provider_edit client_edit key_retire client_access_state \
	admin_access_state role_map_delete role_map_put policy_put client_access_put \
	admin_access_put provider_add role_add user_invite client_add key_add revoke \
	grant_cleanup session_cleanup refresh_cleanup app_cleanup consent_code \
	key_binding secret_rekey ssf_delivery_add
zumd_saga_objects = \
	$(addprefix $(zumd_objdir)/zumd-zumd_saga_,$(addsuffix .$(OBJEXT),$(zumd_saga_names))) \
	$(zumd_objdir)/zumd-zumd_saga_catalog.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_saga_submit.$(OBJEXT)

$(zumd_saga_objects): $(zumd_objdir)/zumd-zumd_saga_%.$(OBJEXT): $(top_srcdir)/zum/src/zumd_saga_%.cc $(top_srcdir)/zdb/src/ZdbSaga.hh $(top_srcdir)/zdb/src/Zdb.hh
	$(MAKE) -C $(zumd_objdir) $(@F)

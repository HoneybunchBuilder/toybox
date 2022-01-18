vcpkg_from_github(
	OUT_SOURCE_PATH SOURCE_PATH
	REPO wolfpld/tracy
	REF 07778badcced109b8190805fbf2d7abfaef0d3b9
	SHA512 17f52d2b9558a2a6ebada5a405f74a8c6eb51c8dfb794a182cf7635cbe48206e1edce3bf6f5e9358ec8b24e9c0cba4019c8eb14bb37524ea762a80aece04652f
	HEAD_REF master
	PATCHES
		fix-switch.patch
)

file(COPY ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt DESTINATION ${SOURCE_PATH})

vcpkg_check_features(
	OUT_FEATURE_OPTIONS FEATURE_OPTIONS
	FEATURES
		on-demand TRACY_ON_DEMAND
		callstack TRACY_CALLSTACK
		only-localhost TRACY_ONLY_LOCALHOST
		no-broadcast TRACY_NO_BROADCAST
		no-code-transfer TRACY_NO_CODE_TRANSFER
		no-context-switch TRACY_NO_CONTEXT_SWITCH
		no-exit TRACY_NO_EXIT
		no-frame-image TRACY_NO_FRAME_IMAGE
		no-sampling TRACE_NO_SAMPLING
		no-verify TRACY_NO_VERIFY
		no-vsync-capture TRACY_NO_VSYNC_CAPTURE
)

vcpkg_configure_cmake(
	SOURCE_PATH ${SOURCE_PATH}
	PREFER_NINJA
	OPTIONS
		${FEATURE_OPTIONS}
)

vcpkg_install_cmake()
vcpkg_fixup_cmake_targets(CONFIG_PATH lib/cmake/${PORT})

vcpkg_copy_pdbs()

file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/debug/include)
file(INSTALL ${SOURCE_PATH}/LICENSE DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT} RENAME copyright)
file(INSTALL ${CMAKE_CURRENT_LIST_DIR}/usage DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT})
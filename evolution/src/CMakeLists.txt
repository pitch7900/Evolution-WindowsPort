add_subdirectory(addressbook)
add_subdirectory(calendar)
add_subdirectory(composer)
add_subdirectory(em-format)
add_subdirectory(e-util)
add_subdirectory(libemail-engine)
add_subdirectory(libgnomecanvas)
add_subdirectory(mail)
add_subdirectory(modules)
add_subdirectory(plugins)
add_subdirectory(shell)

# WebView2 (Windows) has no in-process web extensions: the JavaScript files
# they load are injected into the pages instead.
if(NOT WIN32)
	add_subdirectory(web-extensions)
endif(NOT WIN32)

if(ENABLE_SMIME)
	add_subdirectory(smime)
endif(ENABLE_SMIME)

You can compile on Linux using one of the following options

1) Use Code::Blocks
2) Compile with its GUI

OR

1) Change the directory to "source/src"
2) "make *" where * is:
	all - both of the following
	client - client game
	server - server application
	client_install - client game + automatic copy
	server_install - server application + automatic copy
3) Look for native_client or native_server.

NOTE: If there is an X11 bug, add -lX11 to the CLIENT_LIBS option.
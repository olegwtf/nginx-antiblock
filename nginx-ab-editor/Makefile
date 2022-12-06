default:
	gcc remote.c -o nginx-ab-editor-remote
	mipsel-linux-gnu-gcc-8 local.c -o nginx-ab-editor-local -static

debug:
	gcc remote.c -o nginx-ab-editor-remote -ggdb
	gcc local.c -o nginx-ab-editor-local -ggdb

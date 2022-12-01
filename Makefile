default:
	gcc nginx-ab-editor.c -o nginx-ab-editor

debug:
	gcc nginx-ab-editor.c -o nginx-ab-editor -ggdb

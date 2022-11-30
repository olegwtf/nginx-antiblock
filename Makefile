debug:
	gcc nginx-ab-editor.c -o nginx-ab-editor -ggdb

all:
	gcc nginx-ab-editor.c -o nginx-ab-editor

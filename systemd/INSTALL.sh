#!/bin/bash

ln -s ../nginx-ab-editor/nginx-ab-editor.service /etc/systemd/system/nginx-ab-editor.service
systemctl enable nginx-ab-editor
systemctl start nginx-ab-editor

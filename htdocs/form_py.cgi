#!/usr/bin/python
import cgi

def main():
	form= cgi.FieldStorage()	#parsequery
	if form.has_key("firstname") and form["firstname"].value != "":
		print "<h1>Hello", form["firstname"].value, "</h1>"
	else:
		print "<h1>Error!Please enter firstname.</h1>"

main()
import os


def create_html():
   HTML = """<!DOCTYPE html>
   <html>
   <head>
   <title>Test</title>
   <head>
   <body>hello</body>
   </html>"""
   FILE_NAME = "test.html"
   with open(FILE_NAME, 'w', encoding='utf-8') as file:
    file.write(HTML)
   

create_html()

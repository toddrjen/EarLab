rmdir /s /q "Interim Setup Files"
mkdir "Interim Setup Files"
svn export --force ..\Documentation "Interim Setup Files\Documentation"
svn export --force ..\ModelFolders "Interim Setup Files\ModelFolders"
svn export --force "..\Required Support Files" "Interim Setup Files\Required Support Files"
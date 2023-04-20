all: myftpserver myftp

myftpserver: server_main.cpp
		g++ -Wall -g -std=c++11 -pthread -o myftpserver server_main.cpp

myftp: client_main.cpp
		g++ -Wall -g -std=c++11 -pthread -o myftp client_main.cpp

clean:
		rm -f myftpserver myftp

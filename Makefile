nick: player.c
	arch -x86_64 gcc -o main player.c `pkg-config --libs libavformat libavcodec libavutil libswscale --cflags gtk4` && ./main dog.mp4 30
matt: player.c
	gcc -o main player.c `pkg-config --libs libavformat libavcodec libavutil libswscale --cflags gtk4` && ./main dog.mp4 30
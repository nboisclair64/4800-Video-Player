nick: player.c
	arch -x86_64 gcc -o main player.c `pkg-config --libs libavformat libavcodec libavutil libswscale --cflags gtk4` && ./main bar-rescue.mp4 30
matt1: player.c
	gcc -o main player.c `pkg-config --libs libavformat libavcodec libavutil libswscale --cflags gtk4` && ./main bar-rescue.mp4 30
matt2: player.c
	gcc -o main player.c `pkg-config --libs libavformat libavcodec libavutil libswscale --cflags gtk4` && ./main dog.mp4 30
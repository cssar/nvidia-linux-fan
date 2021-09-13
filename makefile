all:
	gcc -lXNVCtrl -lX11 -lm main.c -o nvidia-fan-curve

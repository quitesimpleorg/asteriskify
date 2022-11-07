asteriskify: asteriskify.c
	$(CC) -Wall -Wextra -static -o asteriskify asteriskify.c
	
clean: asteriskify
	rm -f asteriskify

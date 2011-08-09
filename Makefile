NAME    = las
DEPS    = libavformat libavutil libavcodec
CFLAGS += -Wall -Wextra -Wshadow -O3 -ffast-math -std=c99 `pkg-config --cflags $(DEPS)`
LDLIBS += `pkg-config --libs $(DEPS)`
all: $(NAME)
$(NAME): $(NAME).o
clean:
	$(RM) $(NAME).o
distclean: clean
	$(RM) $(NAME)
re: distclean $(NAME)

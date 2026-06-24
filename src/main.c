
#include "interpreter.h"
#include "master_slave_channel.h"
#include "primitives.h"
#include "sexp.h"

#include <stdio.h>

int main(int argc, char **argv) {
	const char *in_path = NULL;
	for (int i = 1; i < argc; ++i) {
		in_path = argv[i];
		break;
	}
	FILE *stream = in_path ? fopen(in_path, "r") : stdin;
	if (!stream) {
		fprintf(stderr, "Could not open %s for reading\n", in_path);
		return 1;
	}
	Interpreter *I = interpreter_new(primitives_default(), 1);
	interpreter_set_stream(I, stream);
	// spawn a single interpreter
	ChannelPair *channel = interpreter_begin(I, NULL, NULL);
	while (1) {
		Sexp *msg = master_await_msg(channel);
		if (sexp_is_nil(*msg)) goto all_done;
	}
all_done:
	fclose(stream);
	return 0;
}


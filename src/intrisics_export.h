Primitives *primitives_default() {
	Primitives *prims = primitives_new();
	prims->default_eval_binding = lps_from_cstr("p-exec");
	primitive_reader_macros(prims);
	primitives_set_macro(prims, lps_from_cstr("p-" "exec"), p_exec);
	return prims;
}


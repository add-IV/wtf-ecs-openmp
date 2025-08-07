# NOTE: for release build use gcc & mold at -O2
CC := gcc -std=gnu17
ifeq ($(OS),Windows_NT)
	# CC := clang -std=gnu99
	RE := rg
else
	# CC := gcc -std=gnu99
	RE := grep
endif
# CFLAGS := -Wall -Wextra -Wno-int-conversion -fpermissive -fdiagnostics-show-option -ggdb -O0
CFLAGS := -Wall -Wextra -Wno-int-conversion -fpermissive -fdiagnostics-show-option -fopenmp -O2
LIBS := -lpthread
LDFLAGS := -fuse-ld=lld -flto -static
SRC := $(wildcard src/*.c)
SRC += $(wildcard src/*/*.c)
OBJ := $(patsubst src/%.c,obj/%.o,$(SRC))

.PHONY: default dependencies clean clean-dep clean-emacs clean-all dep_dirs obj_dirs lsp-test tests

default: tests

tests: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS) $(LDFLAGS)

test: tests
	${CURDIR}/$<

obj/%.o: src/%.c dep/%.d | obj_dirs
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBS)

dependencies: $(DEP)

dep/%.d: src/%.c | dep_dirs
	$(CC) -MM -MP -MT obj/$*.o -MF $@ $< $(LIBS)

# include $(wildcard dep/*.d)
include $(wildcard dep/*/*.d)

dep_dirs:
	@mkdir -p dep/
	@mkdir -p dep/allocators

obj_dirs:
	@mkdir -p obj/
	@mkdir -p obj/allocators

clean:
	echo "cleaning"
	rm -rf *.o obj/*.o *.exe tarragon

clean-dep:
	echo "cleaning dependency dir"
	rm -rf dep/*.d

clean-emacs:
	rm -rf *~ src/*~

clean-all: clean clean-dep clean-emacs

compile_commands.json: $(SRC)
	make $(ALL_OBJ) --always-make --dry-run \
	| $(RE) -w '$(CC)' \
	| $(RE) -w '\-[co]' \
	| jq -nR '[inputs|{command:., directory:".", file: match("\\-c [\\w\\-/]+\\.c").string[3:], output: match("\\-o [\\w\\-/]+\\.o").string[3:]}]' > compile_commands.json

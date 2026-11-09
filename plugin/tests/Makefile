# Host-side unit tests (no PS4 toolchain needed): make -C plugin/tests
CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Werror
TESTS = test_trigger_map test_trigger_effects test_game_state test_hero_capture test_two_stage test_pause_dump

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

%: %.c ../source/*.h
	$(CC) $(CFLAGS) -o $@ $< -lm

clean:
	rm -f $(TESTS)

.PHONY: test clean

.PHONY: all module clean check docs

all: module

module:
	$(MAKE) -C driver

clean:
	$(MAKE) -C driver clean

check:
	./scripts/static-check.sh
	python3 -m unittest discover -s tests -p 'test_*.py'

docs:
	./scripts/check-docs.sh

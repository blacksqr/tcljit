ROOT = monografia

all: monografia

monografia:
	latex $(ROOT)
	latex $(ROOT)
	bibtex $(ROOT)
	bibtex $(ROOT)
	makeindex $(ROOT).nlo -s nomencl.ist -o $(ROOT).nls
	latex $(ROOT)
	latex $(ROOT)
	latex $(ROOT)
	dvipdf -o$(ROOT).pdf $(ROOT).dvi

clean:
	rm -rf *~ *.aux *.dvi *.bbl *.blg *.log *.pdf *.lot *.lof *.toc

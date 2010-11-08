ROOT = monografia

all: monografia

monografia:
	latex $(ROOT)
	bibtex $(ROOT)
	bibtex $(ROOT)
	latex $(ROOT)
	latex $(ROOT)
	dvipdf -o$(ROOT).pdf $(ROOT).dvi

clean:
	rm -rf *~ *.aux *.dvi *.bbl *.blg *.log *.pdf *.lot *.lof *.toc

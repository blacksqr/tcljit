ROOT = projeto
OTHERS = resumo conteudo

all:
	pdflatex $(ROOT) $(OTHERS)
	bibtex $(ROOT)
	bibtex $(ROOT)
	pdflatex $(ROOT) $(OTHERS)
	pdflatex $(ROOT) $(OTHERS)
#dvipdf -o$(ROOT).pdf $(ROOT).dvi

clean:
	rm -rf *~ *.aux *.dvi *.bbl *.blg *.log *.pdf *.lot *.lof *.toc

ROOT = relatorio
OTHERS = resumo conteudo
LATEX = latex

all:
	dot -Teps a1test1.dot > a1test1.ps
	$(LATEX) $(ROOT) $(OTHERS)
	bibtex $(ROOT)
	bibtex $(ROOT)
	$(LATEX) $(ROOT) $(OTHERS)
	$(LATEX) $(ROOT) $(OTHERS)
	dvipdf -o$(ROOT).pdf $(ROOT).dvi

clean:
	rm -rf *~ *.aux *.dvi *.bbl *.blg *.log *.pdf *.lot *.lof *.toc

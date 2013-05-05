rm -rf *.out *.aux *.log *.~ *.blg *.dvi *.ps *.bbl *.toc *.loa *.lof *.nav *.snm *.vrb
latex  gvn-sem-7-presentation.tex
latex  gvn-sem-7-presentation.tex
dvips -o  gvn-sem-7-presentation.ps  gvn-sem-7-presentation.dvi
ps2pdf  gvn-sem-7-presentation.ps
rm -rf *.out *.aux *.log *.~ *.blg *.dvi *.ps *.bbl *.toc *.loa *.lof *.nav *.snm *.vrb


# The showcase site: static files behind nginx. No build step; what is in this directory is
# what ships.
FROM nginx:1.29-alpine

COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY index.html styles.css main.js /usr/share/nginx/html/
COPY img /usr/share/nginx/html/img
COPY fonts /usr/share/nginx/html/fonts

HEALTHCHECK --interval=10s --timeout=3s --retries=3 \
    CMD wget -q -O /dev/null http://127.0.0.1/ || exit 1

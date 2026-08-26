FROM golang:1.23-alpine AS build
WORKDIR /src/simulator
COPY simulator/go.mod .
COPY simulator/main.go .
RUN CGO_ENABLED=0 go build -o /simulator .

FROM alpine:3.20
COPY --from=build /simulator /simulator
ENTRYPOINT ["/simulator"]

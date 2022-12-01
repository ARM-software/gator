# Docker image

Here you can find help on how to use docker image included in this repository.

## Images

There are two docker images:

- `hwcpipe2` for building, linting and running hwcpipe2
- `hwcpipe2-pre-commit` for running [pre-commit.com](https://pre-commit.com) checkers.

## docker-compose

All docker containers and their lifecycle is driven by the [docker-compose](https://docs.docker.com/compose/) utility.

Use `docker-compose build` to build all the images, `docker-compose pull` to pull the images from artifactory
and `docker-compose push` to push images built to artifactory.

Run `docker-compose run hwcpipe2` to run the hwcpipe2 image, or `docker-compose run hwcpipe2-pre-commit` for the
pre-commit image.

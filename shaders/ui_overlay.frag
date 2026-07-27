#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uUiTexture;
void main() {
    // Cairo UI is premultiplied ARGB uploaded as BGRA.
    fragColor = texture(uUiTexture, vec2(vUv.x, 1.0 - vUv.y));
}

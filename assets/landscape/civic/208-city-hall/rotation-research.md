# Rotation prompting research, 2026-10-14

Primary sources consulted:

- https://developers.openai.com/api/docs/guides/image-prompting
- https://developers.openai.com/api/docs/guides/image-generation

Applied guidance: give each image a specific role, state the intended edit separately from preserved
features, and iterate one view at a time using prior outputs as supporting references. Describe the
visible walls and occlusion rather than relying on an angle alone. The official guide explicitly
treats camera specifications as appearance cues rather than exact physical simulation.

The built-in image-generation tool available in this chat exposes prompt, reference-image paths,
recent-image count and background transparency. It exposes no model, quality or reasoning-effort
control. None was raised. API quality controls described in the documentation are separate from
the controls exposed by this tool; the API/CLI fallback has not been used.

The first three-reference rotation studies produced distinct corners but stretched the overall
site into a narrow rectangle. They are rejected for production. The square-site studies add an
explicit 3x3-tile diamond reference. They remain artwork candidates: equal scale, plan continuity,
roof fixtures, full site geometry and exact tile-mask alignment must be checked before integration.

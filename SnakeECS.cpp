#include <iostream>
#include <random>

#include "SFML/Graphics.hpp"

#include "entt/entt.hpp"
#include "Components.h"
#include <fstream>


constexpr auto tileSize = 32.0f;
constexpr auto fieldSize = 20;
constexpr auto halfFieldSize = fieldSize / 2;
constexpr auto textureSize = 40.0f;

constexpr float lerp(const float a, const float b, const float f) {
	return a + f * (b - a);
}

int movementSpeed = 6;
float moveSpeed() { return lerp(.2f, .1f, float(movementSpeed) / 10.0f); }

// move 25 blocks before it moves
static float specialFruitTime() { return moveSpeed() * 25.0f; }


enum class GameState
{
	Running,
	Paused,
	Won,
	GameOver
};

sf::Font openSansFont;

entt::registry registry{};
GameState gameState;
size_t score;


bool operator==(const Position& lhs, const Position& rhs)
{
	return lhs.x == rhs.x && lhs.y == rhs.y;
}


void drawRectangle(sf::VertexArray& vertices, const Position position, const sf::Color& color)
{
	// contracted rect
	const sf::FloatRect rect(float(position.x) * tileSize, float(position.y) * tileSize, tileSize,
		tileSize);
	const sf::FloatRect uv(0, 0, textureSize, textureSize);

	const auto tl = sf::Vertex(sf::Vector2f(rect.left, rect.top), color, sf::Vector2f(uv.left, uv.top));
	const auto tr = sf::Vertex(sf::Vector2f(rect.left + rect.width, rect.top), color,
		sf::Vector2f(uv.left + uv.width, uv.top));
	const auto br = sf::Vertex(sf::Vector2f(rect.left + rect.width, rect.top + rect.width), color,
		sf::Vector2f(uv.left + uv.width, uv.top + uv.height));
	const auto bl = sf::Vertex(sf::Vector2f(rect.left, rect.top + rect.width), color,
		sf::Vector2f(uv.left, uv.top + uv.height));

	// North-east triangle
	vertices.append(tl);
	vertices.append(tr);
	vertices.append(br);

	// South west triangle
	vertices.append(br);
	vertices.append(bl);
	vertices.append(tl);
}

void createSnakeVertices()
{
	auto view = registry.view<Snake, VertexList>();
	static const auto color = sf::Color::Blue;
	for (auto entity : view)
	{
		auto& segments = view.get<Snake>(entity).segments;
		auto& vertices = view.get<VertexList>(entity);
		vertices.vertices.clear();
		for (auto&& segment : segments)
		{
			drawRectangle(vertices.vertices, segment.currentPosition(), color);
		}
	}
}

void createFruitVertices()
{
	auto view = registry.view<Fruit, Position, VertexList>();
	static const auto color = sf::Color::Green;
	static const auto specialColor = sf::Color::Red;
	for (auto entity : view)
	{
		const auto& position = view.get<Position>(entity);
		auto& vertices = view.get<VertexList>(entity);
		vertices.vertices.clear();
		drawRectangle(vertices.vertices, position, view.get<Fruit>(entity).type == FruitType::Regular ? color : specialColor);
	}
}

void updateSnake(const sf::Time& dt)
{
	auto view = registry.view<Snake, Time>();
	for (auto entity : view)
	{
		auto&& [snake, timer] = view.get<Snake, Time>(entity);

		timer.time += dt;

		// moving at all?
		if (snake.direction == Direction::None) { continue; }

		// able to move?
		if (timer.time.asSeconds() >= moveSpeed() && !snake.segments.empty())
		{
			timer.time = sf::Time::Zero;

			auto headPosition = snake.segments.at(0).currentPosition();
			switch (snake.direction)
			{
			case Direction::North:
				--headPosition.y;
				break;
			case Direction::East:
				++headPosition.x;
				break;
			case Direction::South:
				++headPosition.y;
				break;
			case Direction::West:
				--headPosition.x;
				break;
			default: ;
			}

			// check if outside field
			if (headPosition.x < 0 || headPosition.y < 0 || headPosition.x >= fieldSize || headPosition.y >= fieldSize)
			{
				gameState = GameState::GameOver;
				return;
			}

			// check for self-collision
			if (snake.segments.size() > 1)
			{
				const auto end = snake.segments.end();
				for (auto it = ++snake.segments.begin(); it != end; ++it)
				{
					if (it->currentPosition() == headPosition)
					{
						gameState = GameState::GameOver;
						return;
					}
				}
			}

			// Move the snake
			const auto count = snake.segments.size();
			for (size_t i = 0; i < count; ++i)
			{
				if (i == 0)
				{
					snake.segments.at(i).setPosition(headPosition);
					continue;
				}
				snake.segments.at(i).setPosition(snake.segments.at(i - 1).previousPosition());
			}

			// check for extensions
			if (snake.extensionsLeft > 0)
			{
				snake.segments.emplace_back(snake.segments.back().previousPosition());
				--snake.extensionsLeft;
			}
			createSnakeVertices();
		}
	}
}


void createFruit(const FruitType type)
{
	static const auto maxSize = size_t(fieldSize * fieldSize);

	// check if there's only snake on field
	// and store positions of segments
	size_t snakeLength = 0;
	std::vector<Position> segments{};
	auto snakeView = registry.view<Snake>();
	for (auto entity : snakeView)
	{
		auto& snake = snakeView.get(entity);
		snakeLength += snake.segments.size();
		for (auto&& segment : snake.segments)
		{
			segments.emplace_back(segment.currentPosition());
		}
	}

	if (snakeLength >= maxSize)
	{
		gameState = GameState::Won;
		return;
	}

	// initialize random
	static std::random_device randomDevice{};
	static std::default_random_engine engine(randomDevice());
	static const std::uniform_int_distribution<int> intDistribution(0, maxSize - 1);

	// try to place a fruit
	size_t retriesLeft = 10000;
	while (retriesLeft > 0)
	{
		// to avoid getting stuck in an infinite loop
		--retriesLeft;

		// generate position and see if it's taken or not by a segment
		const auto pos = intDistribution(engine);
		const auto position = Position{pos % fieldSize, pos / fieldSize};
		auto takenPosition = false;
		for (auto&& segmentPosition : segments)
		{
			if (segmentPosition == position)
			{
				takenPosition = true;
				break;
			}
		}

		if (takenPosition) continue;

		// not taken so place it
		auto [entity, fruit, fruitPosition, time, score, vertices] = registry.create<Fruit, Position, Time, Score, VertexList>();
		fruit.type = type;
		fruitPosition = position;
		time.time = sf::seconds(specialFruitTime());
		score.amount = type == FruitType::Regular ? 1 : 3;
		vertices.vertices.setPrimitiveType(sf::Triangles);
		createFruitVertices();
		return;
	}
}

void createFruitSpawner()
{
	static std::random_device randomDevice{};
	static std::default_random_engine engine(randomDevice());
	static const std::uniform_real_distribution<float> distribution(8.0f, 26.0f);

	auto [entity, spawner] = registry.create<FruitSpawner>();
	// random delay
	spawner.remaining = sf::seconds(distribution(engine) * moveSpeed());
}

void updateFruitSpawners(const sf::Time& dt)
{
	auto view = registry.view<FruitSpawner>();
	for (auto entity : view)
	{
		auto& spawner = view.get(entity);
		spawner.remaining -= dt;
		if (spawner.remaining <= sf::Time::Zero)
		{
			createFruit(FruitType::Special);
			registry.destroy(entity);
		}
	}
}

void updateFruits(const sf::Time& dt)
{
	auto view = registry.view<Fruit, Time>();

	for (auto entity : view)
	{
		auto&& [fruit, countdown] = view.get<Fruit, Time>(entity);

		if (fruit.type == FruitType::Special)
		{
			countdown.time -= dt;

			if (countdown.time <= sf::Time::Zero)
			{
				registry.destroy(entity);
				createFruitSpawner();
			}
		}
	}
}

void updateCollisions()
{
	// check against snake
	auto snakeView = registry.view<Snake>();
	for (auto snakeEntity : snakeView)
	{
		auto& snake = snakeView.get(snakeEntity);
		const auto& headPosition = snake.segments.at(0).currentPosition();

		auto fruitView = registry.view<Fruit, Position, Score>();
		for (auto fruitEntity : fruitView)
		{
			const auto& [fruit, fruitPosition, fruitScore] = fruitView.get<Fruit, Position, Score>(fruitEntity);
			if (headPosition == fruitPosition)
			{
				score += fruitScore.amount;
				snake.extensionsLeft += fruitScore.amount;

				switch (fruit.type)
				{
				case FruitType::Regular:
					createFruit(FruitType::Regular);
					break;
				case FruitType::Special:
					createFruitSpawner();
					break;
				default: ;
				}

				registry.destroy(fruitEntity);
			}
		}
	}
}

void createSnake()
{
	auto [id, snake, time, vertices] = registry.create<Snake, Time, VertexList>();
	time.time = sf::Time::Zero;
	const auto center = Position{halfFieldSize, halfFieldSize};

	snake.segments.emplace_back(Segment(center));
	snake.extensionsLeft = 2;
	vertices.vertices.setPrimitiveType(sf::Triangles);
	vertices.vertices.resize(fieldSize * fieldSize * 6);
	createSnakeVertices();
}

void startNewGame()
{
	registry.reset();
	gameState = GameState::Running;
	score = 0;

	createSnake();
	createFruit(FruitType::Regular);
	createFruitSpawner();
}

void handleEvent(sf::Event& event)
{
	if (event.type == sf::Event::KeyPressed)
	{
		if (gameState == GameState::Running) {
			auto view = registry.view<Snake>();
			for (auto entity : view)
			{
				auto& direction = view.get(entity).direction;

				switch (event.key.code)
				{
				case sf::Keyboard::W:
				case sf::Keyboard::Up:
					if (direction != Direction::South)
						direction = Direction::North;
					break;

				case sf::Keyboard::A:
				case sf::Keyboard::Left:
					if (direction != Direction::East)
						direction = Direction::West;
					break;

				case sf::Keyboard::S:
				case sf::Keyboard::Down:
					if (direction != Direction::North)
						direction = Direction::South;
					break;

				case sf::Keyboard::D:
				case sf::Keyboard::Right:
					if (direction != Direction::West)
						direction = Direction::East;
					break;
				default:;
				}
			}
		}

		if (event.key.code == sf::Keyboard::P || event.key.code == sf::Keyboard::Escape)
		{
			if (gameState == GameState::Running)
			{
				gameState = GameState::Paused;
			}
			else if (gameState == GameState::Paused)
			{
				gameState = GameState::Running;
			}
		}
	}
}

void renderVertices(sf::RenderTarget& renderer, sf::RenderStates& states)
{
	auto view = registry.view<VertexList>();

	for (auto entity : view)
	{
		 auto& entityVertices = view.get(entity).vertices;
		 renderer.draw(entityVertices, states);
	}
}


int main()
{
	sf::RenderWindow window;
	window.create(sf::VideoMode(int(fieldSize * tileSize), int(fieldSize * tileSize)), "Psy's Snake");
	window.setFramerateLimit(240);

	sf::Clock clock;
	sf::Time elapsed;

	sf::RenderStates renderState = sf::RenderStates::Default;
	sf::Texture texture;
	if (texture.loadFromFile("texture.png"))
	{
		renderState.blendMode = sf::BlendAlpha;
		renderState.texture = &texture;
	}
	openSansFont.loadFromFile("OpenSansBold.ttf");

	sf::Text wonText;
	wonText.setString("You've Won!");
	wonText.setFont(openSansFont);
	wonText.setCharacterSize(50);
	wonText.setFillColor(sf::Color::White);
	wonText.setOutlineThickness(1.0f);
	wonText.setOutlineColor(sf::Color::Black);

	auto wonBounds = wonText.getLocalBounds();
	wonText.setOrigin(wonBounds.width / 2, wonBounds.height / 2);
	wonText.setPosition(fieldSize * tileSize / 2, fieldSize * tileSize / 2);

	sf::Text gameOverText;
	gameOverText.setString("Game Over!");
	gameOverText.setFont(openSansFont);
	gameOverText.setCharacterSize(50);
	gameOverText.setFillColor(sf::Color::White);
	gameOverText.setOutlineThickness(1.0f);
	gameOverText.setOutlineColor(sf::Color::Black);

	auto gameOverBounds = gameOverText.getLocalBounds();
	gameOverText.setOrigin(gameOverBounds.width / 2, gameOverBounds.height / 2);
	gameOverText.setPosition(fieldSize * tileSize / 2, fieldSize * tileSize / 2);

	sf::Text pausedText;
	pausedText.setString("Paused");
	pausedText.setFont(openSansFont);
	pausedText.setCharacterSize(50);
	pausedText.setFillColor(sf::Color::White);
	pausedText.setOutlineThickness(1.0f);
	pausedText.setOutlineColor(sf::Color::Black);

	auto pausedBounds = pausedText.getLocalBounds();
	pausedText.setOrigin(pausedBounds.width / 2, pausedBounds.height);
	pausedText.setPosition(fieldSize * tileSize / 2, fieldSize * tileSize / 2);

	sf::Text movementSpeedText;
	movementSpeedText.setString("Movement Speed: "+std::to_string(movementSpeed));
	movementSpeedText.setFont(openSansFont);
	pausedText.setCharacterSize(42); // answer to everything
	movementSpeedText.setFillColor(sf::Color::White);
	movementSpeedText.setOutlineThickness(1.0f);
	movementSpeedText.setOutlineColor(sf::Color::Black);
	
	auto movementSpeedTextBounds = movementSpeedText.getLocalBounds();
	movementSpeedText.setOrigin(movementSpeedTextBounds.width / 2, 0);
	movementSpeedText.setPosition(fieldSize * tileSize / 2, fieldSize * tileSize / 2);
	
	startNewGame();

	size_t previousScore = 120;
	while (window.isOpen())
	{
		if (score != previousScore)
		{
			previousScore = score;
			std::string title = "Psy's Snake - Score: ";
			title.append(std::to_string(score));
			window.setTitle(title);
		}

		// handle events
		sf::Event event{};
		while (window.pollEvent(event))
		{
			handleEvent(event);
			if (event.type == sf::Event::KeyPressed)
			{
				if (event.key.code == sf::Keyboard::N)
				{
					startNewGame();
				}

				if(gameState == GameState::Paused)
				{
					if(event.key.code == sf::Keyboard::Left)
					{
						if (movementSpeed > 1) {
							const auto original = moveSpeed();
							--movementSpeed;
							movementSpeedText.setString("Movement Speed: " + std::to_string(movementSpeed));
							const auto ratio = moveSpeed() / original;
							auto fruitView = registry.view<Fruit, Time>();
							
							for (auto fruitEntity : fruitView)
							{
								auto&& [fruit, time] = fruitView.get<Fruit, Time>(fruitEntity);
								time.time *= ratio;
							}
						}
					}
					if (event.key.code == sf::Keyboard::Right)
					{
						if (movementSpeed < 10) {
							const auto original = moveSpeed();
							++movementSpeed;
							movementSpeedText.setString("Movement Speed: " + std::to_string(movementSpeed));
							const auto ratio = moveSpeed() / original;
							auto fruitView = registry.view<Fruit, Time>();

							for (auto fruitEntity : fruitView)
							{
								auto&& [fruit, time] = fruitView.get<Fruit, Time>(fruitEntity);
								time.time *= ratio;
							}
						}
					}
				}
			}



			if (event.type == sf::Event::Closed)
			{
				registry.reset();
				window.close();
			}
		}

		// update
		elapsed = clock.restart();

		if(!window.hasFocus())
		{
			gameState = GameState::Paused;
		}
		
		if (gameState == GameState::Running)
		{
			updateSnake(elapsed);
			updateFruits(elapsed);
			updateCollisions();
			updateFruitSpawners(elapsed);
		}

		// render
		window.clear();

		renderVertices(window, renderState);

		switch (gameState)
		{
		case GameState::Paused:
			window.draw(pausedText);
			window.draw(movementSpeedText);
			break;
		case GameState::Won:
			window.draw(wonText);
			break;
		case GameState::GameOver:
			window.draw(gameOverText);
			break;
		default: ;
		}

		window.display();
	}

	return EXIT_SUCCESS;
}
